/*
 * Copyright 2025 Karesis
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */



#include "analysis/dom_tree.h"
#include "analysis/cfg.h"
#include "ir/value.h"
#include "utils/bump.h"
#include "utils/id_list.h"

#include <assert.h>
#include <stdio.h>





/**
 * @brief [辅助] 通过 IRBasicBlock* 获取 DomTreeNode*
 */
static inline DomTreeNode *
get_dom_node(DominatorTree *tree, IRBasicBlock *bb)
{
  if (!bb)
    return NULL;


  CFGNode *cfg_node = cfg_get_node(tree->cfg, bb);
  if (!cfg_node)
  {


    return NULL;
  }



  return tree->nodes[cfg_node->id];
}












static void
lt_dfs(DominatorTree *tree, DomTreeNode *n, int *current_dfs_num)
{

  int n_num = *current_dfs_num;
  *current_dfs_num = n_num + 1;

  assert(n_num <= tree->cfg->num_nodes && "DFS found more nodes than cfg->num_nodes!");

  n->dfs_num = n_num;
  n->semi_dom = n_num;
  n->label = n;
  tree->dfs_order[n_num] = n;


  IDList *iter;
  list_for_each(&n->cfg_node->successors, iter)
  {
    CFGEdge *succ_edge = list_entry(iter, CFGEdge, list_node);
    CFGNode *succ_cfg_node = succ_edge->node;

    assert(succ_cfg_node->id >= 0 && succ_cfg_node->id < tree->cfg->num_nodes &&
           "CFG successor ID is out of bounds (negative or >= num_nodes)!");


    DomTreeNode *w = tree->nodes[succ_cfg_node->id];


    if (w->dfs_num == 0)
    {
      w->parent = n;
      lt_dfs(tree, w, current_dfs_num);
    }
  }
}






static void
union_find_compress(DomTreeNode *n)
{
  if (n->ancestor->ancestor != NULL)
  {
    union_find_compress(n->ancestor);
    if (n->ancestor->label->semi_dom < n->label->semi_dom)
    {
      n->label = n->ancestor->label;
    }
    n->ancestor = n->ancestor->ancestor;
  }
}

/**
 * @brief 查找从 n (不含) 到根的路径上 semi_dom 最小的节点
 */
static DomTreeNode *
union_find_eval(DomTreeNode *n)
{
  if (n->ancestor == NULL)
  {
    return n->label;
  }
  else
  {
    union_find_compress(n);


    return n->label;
  }
}

/**
 * @brief 将子节点 c 链接到父节点 p
 */
static void
union_find_link(DomTreeNode *p, DomTreeNode *c)
{
  c->ancestor = p;

}







static void
lt_compute_semi_dominators(DominatorTree *tree)
{
  int num_nodes = tree->cfg->num_nodes;


  for (int i = num_nodes; i >= 2; i--)
  {
    DomTreeNode *n = tree->dfs_order[i];
    if (!n)
      continue;


    IDList *iter;
    list_for_each(&n->cfg_node->predecessors, iter)
    {
      CFGEdge *pred_edge = list_entry(iter, CFGEdge, list_node);
      CFGNode *pred_cfg_node = pred_edge->node;
      DomTreeNode *v = tree->nodes[pred_cfg_node->id];
      if (!v)
        continue;




      DomTreeNode *v_prime;
      if (v->dfs_num <= 0)
        continue;

      if (v->dfs_num < n->dfs_num)
      {
        v_prime = v;
      }
      else
      {

        v_prime = union_find_eval(v);
      }



      if (v_prime->semi_dom < n->semi_dom)
      {
        n->semi_dom = v_prime->semi_dom;
      }
    }



    DomTreeNode *s = tree->dfs_order[n->semi_dom];
    BucketNode *bucket_node = BUMP_ALLOC(tree->arena, BucketNode);
    bucket_node->node = n;
    list_add_tail(&s->bucket, &bucket_node->list_node);



    if (n->parent)
    {
      union_find_link(n->parent, n);
    }
  }
}







static void
lt_compute_idominators(DominatorTree *tree)
{
  int num_nodes = tree->cfg->num_nodes;


  for (int i = 2; i <= num_nodes; i++)
  {
    DomTreeNode *n = tree->dfs_order[i];
    if (!n)
      continue;

    DomTreeNode *s = tree->dfs_order[n->semi_dom];


    IDList *iter, *temp;
    list_for_each_safe(&s->bucket, iter, temp)
    {
      BucketNode *bucket_node = list_entry(iter, BucketNode, list_node);
      DomTreeNode *w = bucket_node->node;


      DomTreeNode *u = union_find_eval(w);

      if (u->semi_dom < w->semi_dom)
      {

        w->idom = u;
      }
      else
      {

        w->idom = s;
      }


    }
    list_init(&s->bucket);
  }



  tree->root->idom = NULL;


  for (int i = 2; i <= num_nodes; i++)
  {
    DomTreeNode *n = tree->dfs_order[i];
    if (!n)
      continue;

    if (n->idom != tree->dfs_order[n->semi_dom])
    {

      n->idom = n->idom->idom;
    }


    if (n->idom)
    {
      DomTreeChild *child_node = BUMP_ALLOC(tree->arena, DomTreeChild);
      child_node->node = n;
      list_add_tail(&n->idom->children, &child_node->list_node);
    }
  }
}





DominatorTree *
dom_tree_build(FunctionCFG *cfg, Bump *arena)
{
  if (!cfg || !cfg->entry_node)
  {
    return NULL;
  }

  int num_nodes = cfg->num_nodes;


  DominatorTree *tree = BUMP_ALLOC_ZEROED(arena, DominatorTree);
  tree->cfg = cfg;
  tree->arena = arena;



  tree->nodes = BUMP_ALLOC_SLICE_ZEROED(arena, DomTreeNode *, num_nodes);


  tree->dfs_order = BUMP_ALLOC_SLICE_ZEROED(arena, DomTreeNode *, num_nodes + 1);


  for (int i = 0; i < num_nodes; i++)
  {
    CFGNode *cfg_node = &cfg->nodes[i];
    DomTreeNode *dom_node = BUMP_ALLOC_ZEROED(arena, DomTreeNode);


    dom_node->cfg_node = cfg_node;
    dom_node->dfs_num = 0;
    list_init(&dom_node->children);
    list_init(&dom_node->bucket);


    dom_node->label = dom_node;


    tree->nodes[i] = dom_node;
  }


  tree->root = tree->nodes[cfg->entry_node->id];




  int dfs_counter = 1;
  lt_dfs(tree, tree->root, &dfs_counter);


  int visited_nodes = dfs_counter - 1;
  assert(visited_nodes <= num_nodes && "DFS visited more nodes than cfg->num_nodes reported!");





  lt_compute_semi_dominators(tree);


  lt_compute_idominators(tree);

  return tree;
}

void
dom_tree_destroy(DominatorTree *tree)
{









  (void)tree;
}

bool
dom_tree_dominates(DominatorTree *tree, IRBasicBlock *a, IRBasicBlock *b)
{


  if (a->label_address.kind != IR_KIND_BASIC_BLOCK)
  {
    return true;
  }


  if (a == b)
  {
    return true;
  }

  DomTreeNode *node_a = get_dom_node(tree, a);
  DomTreeNode *node_b = get_dom_node(tree, b);

  if (!node_a || !node_b)
  {

    if (!node_b && b->label_address.kind == IR_KIND_BASIC_BLOCK)
    {
      return false;
    }

    return false;
  }



  DomTreeNode *current = node_b->idom;
  while (current)
  {
    if (current == node_a)
    {
      return true;
    }
    current = current->idom;
  }

  return false;
}

IRBasicBlock *
dom_tree_get_idom(DominatorTree *tree, IRBasicBlock *b)
{
  DomTreeNode *node_b = get_dom_node(tree, b);

  if (node_b && node_b->idom)
  {
    return node_b->idom->cfg_node->block;
  }

  return NULL;
}