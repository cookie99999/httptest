#ifndef __PARSER_H
#define __PARSER_H

struct _htmlatrine_node {
  struct _htmlatrine_node *parent;
  struct _htmlatrine_node *children;
  struct _htmlatrine_node *next_sibling;
  int num_children;
  char *element; // maybe make these 2 const idk how that works
  char *text; //
};
typedef struct _htmlatrine_node htmlatrine_node;

struct _htmlatrine_dom {
  htmlatrine_node *root;
  //probably some metadata later
};
typedef struct _htmlatrine_dom htmlatrine_dom;

htmlatrine_dom htmlatrine_parse(char *buf);

#endif /* __PARSER_H */
