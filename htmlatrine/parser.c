#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"


void htmlatrine_node_add_child(htmlatrine_node *p, htmlatrine_node *c) {
  if (p->children == NULL) {
    p->children = c;
    p->num_children++;
    return;
  }

  htmlatrine_node *n = p->children;
  for (;;) {
    if (n->next_sibling == NULL) {
      n->next_sibling = c;
      p->num_children++;
      return;
    }
    n = n->next_sibling;
  }
}

htmlatrine_node *htmlatrine_node_new(htmlatrine_node *parent) {
  htmlatrine_node *n = malloc(sizeof(htmlatrine_node));
  if (n == NULL)
    return NULL;

  n->num_children = 0;
  n->element = NULL;
  n->text = NULL;
  n->children = NULL;
  n->next_sibling = NULL;
  if (parent == NULL)
    n->parent = NULL;
  else {
    n->parent = parent;
    htmlatrine_node_add_child(parent, n);
  }

  return n;
}

void htmlatrine_node_delete(htmlatrine_node *n) {
  if (n->element != NULL)
    free(n->element);
  if (n->text != NULL)
    free(n->text);
  //TODO: remove from parent
  if (n->num_children > 0 && n->children != NULL) {
    htmlatrine_node *next, *next2;
    next = n->children;
    while (next != NULL) {
      next2 = next->next_sibling;
      htmlatrine_node_delete(next);
      next = next2;
    }
  }
}

htmlatrine_dom htmlatrine_dom_new() {
  htmlatrine_dom d = { .root = NULL };
  return d;
}

void htmlatrine_dom_delete(htmlatrine_dom *d) {
  if (d->root != NULL)
    htmlatrine_node_delete(d->root);
}

htmlatrine_node *htmlatrine_parse_text(char **pos) {
  htmlatrine_node *n = htmlatrine_node_new(NULL);
  int i;
  for (i = 0; (*pos)[i] != '\0' && (*pos)[i] != '<'; i++)
    ;
  if ((n->text = malloc(i + 1)) == NULL)
    return NULL;
  memcpy(n->text, *pos, i);
  n->text[i] = '\0';
  *pos += i;

  return n;
}

htmlatrine_node *htmlatrine_parse_element(char **pos) {
  ++*pos; //skip the opening <
  htmlatrine_node *n = htmlatrine_node_new(NULL); //TODO error check also above
  int i;
  for (i = 0; (*pos)[i] != '\0' && (*pos)[i] != '>' && (*pos)[i] != ' '; i++)
    ;
  if ((n->element = malloc(i + 1)) == NULL)
    return NULL;
  memcpy(n->element, *pos, i);
  n->element[i] = '\0';
  *pos += i;
  *pos = strstr(*pos, ">");
  ++*pos;

  return n;
}

int htmlatrine_consume_element(char **pos, htmlatrine_node *n) {
  char *search;
  int searchlen = strlen("</>");
  searchlen += strlen(n->element);
  if ((search = malloc(++searchlen)) == NULL)
    return -1;
  snprintf(search, searchlen, "</%s>", n->element);

  *pos = strstr(*pos, search); //TODO dies if no matching close tag
  *pos += searchlen - 1;

  return 0;
}

void htmlatrine_consume_whitespace(char **pos) {
  while (isspace(**pos))
    ++*pos;
}

//TODO check for null dereferences etc
htmlatrine_dom htmlatrine_parse(char *buf) {
  htmlatrine_dom dom = htmlatrine_dom_new();

  char *pos = buf, *mark = buf;

  while (*pos != '\0') {
    htmlatrine_consume_whitespace(&pos);
    htmlatrine_node *n;
    if (*pos == '<') {
      if (*(pos + 1) == '!' || *(pos + 1) == '/') { //comments and closing tags
	while (*pos != '>')
	  pos++;
	pos++; //skip closing >
	continue;
      }
      n = htmlatrine_parse_element(&pos);
      /*if (strcmp(n->element, "script") == 0 || strcmp(n->element, "style") == 0) {
	htmlatrine_consume_element(&pos, n);
	htmlatrine_node_delete(n);
	continue;
	}*/
    } else {
      n = htmlatrine_parse_text(&pos);
    }

    if (dom.root == NULL)
      dom.root = n;
    else
      htmlatrine_node_add_child(dom.root, n);
  }
  
  return dom;
}
  
