#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "../httpoop/get.h"

enum Confidence {
  CR_TENTATIVE, CR_CERTAIN
};

//TODO move attribute stuff to own file
struct _attribute {
  char *name;
  char *value;
  struct _attribute *next;
};
typedef struct _attribute attribute;

void delete_attribute(attribute *a) {
  if (a->name != NULL)
    free(a->name);
  if (a->value != NULL)
    free(a->value);
  free(a);
}

attribute *new_attribute(const char *name, const char *value) {
  attribute *a = malloc(sizeof(attribute));
  if (a != NULL) {
    if ((a->name = malloc(strlen(name) + 1)) == NULL) {
      free(a);
      return NULL;
    }
    memcpy(a->name, name, strlen(name));
    a->name[strlen(name)] = '\0';

    if ((a->value = malloc(strlen(value) + 1)) == NULL) {
      free(a);
      return NULL;
    }
    memcpy(a->value, value, strlen(value));
    a->value[strlen(value)] = '\0';

    a->next = NULL;
  }
  return a;
}

attribute *new_attribute_blank() {
  attribute *a = malloc(sizeof(attribute));
  if (a != NULL) {
    a->name = NULL;
    a->value = NULL;
    a->next = NULL;
  }
  return a;
}

void set_attribute_name(attribute *a, char *name, int len) {
  if (a->name != NULL) {
    free(a->name);
    a->name = NULL;
  }

  if ((a->name = malloc(len + 1)) == NULL)
    exit(-1);
  memcpy(a->name, name, len);
  a->name[len] = '\0';

  for (int i = 0; i < len; i++)
    a->name[i] = toupper((unsigned char)(a->name[i]));
}

void set_attribute_value(attribute *a, char *value, int len) {
  if (a->value != NULL) {
    free(a->value);
    a->value = NULL;
  }

  if ((a->value = malloc(len + 1)) == NULL)
    exit(-1);
  memcpy(a->value, value, len);
  a->value[len] = '\0';

  for (int i = 0; i < len; i++)
    a->value[i] = toupper((unsigned char)(a->value[i]));
}

void delete_attribute_list(attribute *a) {
  attribute *next = a->next;
  while (next != NULL) {
    delete_attribute(a);
    a = next;
    next = a->next;
  }
}

void append_attribute(attribute *head, attribute *a) {
  attribute *i = head;
  attribute *next = head->next;
  while (next != NULL) {
    i = next;
    next = i->next;
  }
  i->next = a;
}

attribute *find_attribute(attribute *a, const char *name) {
  while (a != NULL) {
    if (strcmp(a->name, name) == 0)
      break;
    a = a->next;
  }

  return a;
}

attribute *get_attribute(char **pos) {
  attribute *a = new_attribute_blank();

  //whitespace and /
  while(**pos == '\x09' || **pos == '\x0a' || **pos == '\x0c' || **pos == '\x0d'
	|| **pos == ' ' || **pos == '/') {
    ++*pos;
  }

  if (**pos == '>') {
    delete_attribute(a);
    return NULL;
  }

  //otherwise pos = attr name
  set_attribute_name(a, "", 0);
  set_attribute_value(a, "", 0);

  int nl = 0, vl = 0;
  for (;;) {
    if (*pos[nl] == '=' && nl > 0) {
      ++nl;
      set_attribute_name(a, *pos, nl);
      *pos += nl;
      break;
    }

    if ((*pos)[nl] == '\x09' || (*pos)[nl] == '\x0a' || (*pos)[nl] == '\x0c'
	|| (*pos)[nl] == '\x0d' || (*pos)[nl] == ' ') {
      while ((*pos)[nl] == '\x09' || (*pos)[nl] == '\x0a' || (*pos)[nl] == '\x0c'
	|| (*pos)[nl] == '\x0d' || (*pos)[nl] == ' ') {
	++nl;
      }
      if ((*pos)[nl] != '=') {
	set_attribute_name(a, *pos, nl);
	*pos += nl;
	return a;
      }
      ++nl;
      break;
    }

    if ((*pos)[nl] == '/' || (*pos)[nl] == '>') {
      set_attribute_name(a, *pos, nl);
      *pos += nl;
      return a;
    }

    ++nl;
  }
  set_attribute_name(a, *pos, nl);
  *pos += nl;

  //value
  while(**pos == '\x09' || **pos == '\x0a' || **pos == '\x0c' || **pos == '\x0d'
	|| **pos == ' ') {
    ++*pos;
  }

  if (**pos == '\"' || **pos == '\'') {
    char b = **pos;
    ++*pos;
    for (;;) {
      ++vl;
      if ((*pos)[vl] == b) {
	set_attribute_value(a, *pos, vl);
	*pos += vl;
	return a;
      }
    }
  }

  if (**pos == '>')
    return a;

  ++vl;

  for (;;) {
    if ((*pos)[vl] == '\x09' || (*pos)[vl] == '\x0a' || (*pos)[vl] == '\x0c'
	|| (*pos)[vl] == '\x0d' || (*pos)[vl] == ' ' || (*pos)[vl] == '>') {
      set_attribute_value(a, *pos, vl);
      *pos += vl;
      return a;
    }

    ++vl;
  }
}

static void set_encoding(httpoop_response *resp, const char *enc) {
  if (resp->charset != NULL) {
    free(resp->charset);
    resp->charset = NULL;
  }
  
  if ((resp->charset = malloc(strlen(enc) + 1)) == NULL)
    exit(-1);
  memcpy(resp->charset, enc, strlen(enc));
  resp->charset[strlen(enc)] = '\0';
}

int get_encoding(httpoop_response *resp) {
  if (resp->charset != NULL)
    return CR_CERTAIN;

  //bom sniff
  if (resp->buffer[0] == '\xef' && resp->buffer[1] == '\xbb' && resp->buffer[2] == '\xbf') {
    set_encoding(resp, "utf-8");
    return CR_CERTAIN;
  }

  if (resp->buffer[0] == '\xfe' && resp->buffer[1] == '\xff') {
    set_encoding(resp, "utf-16be");
    return CR_CERTAIN;
  }

  if (resp->buffer[0] == '\xff' && resp->buffer[1] == '\xfe') {
    set_encoding(resp, "utf-16le");
    return CR_CERTAIN;
  }

  //prescan
  char *fallback = NULL;
  char *pos = resp->buffer;

  //utf-16 xml declarations
  if (strncmp(pos, "\x3c\x00\x3f\x00\x78\x00", 6) == 0) {
    set_encoding(resp, "utf-16le");
    return CR_CERTAIN;
  }

  if (strncmp(pos, "\x00\x3c\x00\x3f\x00\x78", 6) == 0) {
    set_encoding(resp, "utf-16be");
    return CR_CERTAIN;
  }

  //loop:
  attribute *a, **list;
  list = &a;
  for (;;) {
    if (strncmp(pos, "<!--", 4) == 0) {
      pos = strstr(++pos, "-->");
      if (pos == NULL)
	exit(-1);
      pos += 2; //point at the >
    }

    if (strncasecmp(pos, "<meta", 5) == 0) {
      pos += 5; //strlen "<meta"
      int got_pragma = 0, charset_status = 0;
      char need_pragma = 'n';
      for (;;) {
	a = get_attribute(&pos);
	if (a == NULL)
	  break;

	if (find_attribute(*list, a->name))
	  continue;

	append_attribute(*list, a);

	if (strcmp(a->name, "http-equiv") == 0) {
	  if (strcmp(a->value, "content-type") == 0) {
	    got_pragma = 1;
	  }
	}

	if (strcmp(a->name, "content") == 0) {
	  //todo
	  need_pragma = 't';
	}

	if (strcmp(a->name, "charset") == 0) {
	  //todo
	  need_pragma = 'f';
	}
      }

      if (need_pragma != 'n') {
	if (need_pragma != 't' && got_pragma == 1) {
	  if (charset_status != 0) {
	    if (strcasecmp(resp->charset, "utf-16be") == 0 || strcasecmp(resp->charset, "utf-16le") == 0)
	      set_encoding(resp, "utf-8");
	    if (strcasecmp(resp->charset, "x-user-defined") == 0)
	      set_encoding(resp, "windows-1252");

	    return CR_TENTATIVE;
	  }
	}
      }
    }

    
