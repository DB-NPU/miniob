/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <functional>
#include <set>
#include <array>
#include <unordered_set>

namespace common {

// npu-exp: 请不要修改**模板声明**和**类内 public 方法** 其他可以随意改动
template <size_t K, typename Key, typename Value, typename Hash = std::hash<Key>, typename Pred = std::equal_to<Key>>
class LruKCache {

public:
  LruKCache(size_t reserve = 0)
  {
    if (reserve > 0) {
      searcher_.reserve(reserve);
    }
  }

  ~LruKCache()
  {
    destroy();
  }

  void destroy()
  {
    for (ListNode *node : searcher_) {
      delete node;
    }
    searcher_.clear();
  }

  size_t count() const
  {
    return 0;
  }

  bool get(const Key &key, Value &value)
  {
    return false;
  }

  void put(const Key &key, const Value &value)
  {
  }

  void remove(const Key &key)
  {
  }

  // 只要遍历到每一个 <k,v> 即可，没有任何顺序要求
  void foreach(std::function<bool(const Key &, const Value &)> func)
  {
  }

  // 以 LRU-K 序遍历每一个 <k, v>
  // NOTE: 这里的函数名字并不合理，只是为了与 LruCache 保持一致
  void foreach_reverse(std::function<bool(const Key &, const Value &)> func)
  {
  }

private:
  // npu-exp: 以下只是示例 同学们可以自行更改
  class ListNode {
  public:
    Key key_;
    Value value_;
    size_t times_{0}; // 访问次数
    std::array<size_t, K> history_; // 访问时间

  public:
    ListNode(const Key &key, const Value &value) : key_(key), value_(value)
    {}
  };

  class PListNodeHasher {
  public:
    size_t operator()(ListNode *node) const
    {
      if (node == nullptr) {
        return 0;
      }
      return hasher_(node->key_);
    }

  private:
    Hash hasher_;
  };

  class PListNodePredicator {
  public:
    bool operator()(ListNode *const node1, ListNode *const node2) const
    {
      if (node1 == node2) {
        return true;
      }

      if (node1 == nullptr || node2 == nullptr) {
        return false;
      }

      return pred_(node1->key_, node2->key_);
    }

  private:
    Pred pred_;
  };

  class PListNodeComparator {
  public:
    bool operator()(ListNode *const node1, ListNode *const node2) const
    {
      return *node1 < *node2;
    }
  };

private:
  // npu-exp: 以下只是示例 同学们可以自行更改
  using CacheType = std::set<ListNode *, PListNodeComparator>;
  using SearchType = std::unordered_set<ListNode *, PListNodeHasher, PListNodePredicator>;

  size_t cur_time_{0};
  CacheType cache_; // real cache. maintain LRU-K order
  SearchType searcher_; // impl O(1) find
};

}  // namespace common
