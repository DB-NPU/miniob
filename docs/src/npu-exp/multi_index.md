<h1>Multi_Index赛题讲解</h1>

**赛题描述：**

- 多个字段关联起来称为单个索引。

- 需要支持查看索引。

```sql
create index i_id on t1(id, age);
```

SQL 执行过程主要有词语法解析、语义解析、生成算子、算子执行4个阶段，总体执行过程在`SessionStage::handle_sql`中可以看到。

MiniOB 已经实现了在单列上创建索引，我们可以从单列索引的创建过程入手，分析如何实现多列索引的创建。



### Create Index 流程

*create index* 语句的执行过程主要有词语法解析、语义解析、执行这3个阶段。

下面以 *create index x1 on t1(c2)* 为例介绍 *create index* 语句的执行过程。

##### 词语法解析阶段

*create_index_stmt* 的语法结构如下

```yacc
CREATE INDEX ID ON ID LBRACE ID RBRACE
```

这里 *$3* 位置的 *ID* 是语句中的索引名称 *x1*，*$5* 位置的 *ID* 是语句中索引所在的表 *t1*，*$7* 位置的 *ID* 是语句中索引所在的列 *c2*。

语法解析的结果会存入`ParsedSqlNode`中的`CreateIndexSqlNode`，返回值 *$$* 是`ParsedSqlNode`指针。

```c++
struct CreateIndexSqlNode
{
  std::string index_name;      ///< Index name
  std::string relation_name;   ///< Relation name
  std::string attribute_name;  ///< Attribute name
};
```

可以看到，这里存储了从 *SQL* 中提取出的索引名称、表名称、索引列名称。

##### 语义解析阶段

语义解析阶段主要执行流程在`CreateIndexStmt::create`中。本阶段需要根据输入信息，生成`CreateIndexStmt`对象。

这里需要从`Db`中找到创建索引的 `Table`，以及索引所在列的`FieldMeta`。还需要确认是否以及存在同名索引。

##### 执行阶段

*create index* 语句的执行不需要生成算子。

执行阶段入口是`CreateIndexExecutor::execute`，这里主要是调用了`Table`提供的`create_index`接口。

首先根据索引名称、索引列名称构造`IndexMeta`，并创建 *B+* 树索引。然后扫描表中所有数据，将索引列数据加入到 *B+* 树中。最后将索引结构信息加入到表的`TableMeta`中并落入磁盘文件，索引的创建就完成了。

```c++
RC Table::create_index(Trx *trx, const FieldMeta *field_meta, const char *index_name)
{
	......
  rc = index->create(index_file.c_str(), new_index_meta, *field_meta);
	......
  while (...) {
    rc = index->insert_entry(record.data(), &record.rid());
  }
  ......
}
```

本阶段相对重要的步骤是利用索引列的`FieldMeta`信息创建 *B+* 树，和向 *B+* 树中插入数据。

创建 *B+* 树的主要实现细节在`BplusTreeHandler::create`中。`BplusTreeHandler`类中定义了 *B+* 树的整体结构，与索引实现联系最为紧密的是其中的`IndexFileHeader`和`KeyComparator`。前者保存了 *B+* 树中元素的结构信息，主要是元素的数据类型、长度。后者定义了元素的比较方法。

插入索引数据的主要细节在`BplusTreeHandler::insert_entry`中。这里传入的参数是 *Record.data* 数据中索引列的起始位置、和当前记录的 *RID*（即这条记录在磁盘中的位置信息）。首先从传入数据中提取出索引列数据，与 *RID* 一起组合成 *B+* 树的元素。然后寻找该元素应当插入的位置，最后将元素插入到指定位置。

```c++
RC BplusTreeIndex::insert_entry(const char *record, const RID *rid)
{
  return index_handler_.insert_entry(record + field_meta_.offset(), rid);
}

RC BplusTreeHandler::insert_entry(const char *user_key, const RID *rid)
{
  MemPoolItem::unique_ptr pkey = make_key(user_key, *rid);
	......
  RC rc = find_leaf(latch_memo, BplusTreeOperationType::INSERT, key, frame);
  ......
  rc = insert_entry_into_leaf_node(latch_memo, frame, key, rid);
	......
}
```



## Multi_Index 实现概述

以 *create index on t1 (c1, c2)* 为例。

*create index* 与 *create table* 语句结构较为类似，因此实现 *multi-index* 过程中对于多列信息的处理可以参考 *create table* 语句。

##### 词语法解析阶段

创建 *multi-index* 的语法结构大体如下：

```sql
CREATE INDEX idx_name ON table_name (column_list);
```

这里的*column_list*可以是一个ID，也可以是多个，因此存储语法解析结果的`CreateIndexSqlNode`的结构需要做对应改动，使其可以保存多个*column_name*。语法规则的实现可以参照 *create table* 语句中的 *attr_def_list*。

##### 语义解析阶段

这里需要对`CreateIndexStmt`的结构进行相应的修改，使其能够存储多个 *FieldMeta*。

##### 执行阶段

在`Table::create_index`创建索引过程中，首先构造了用于存放索引元数据信息的`IndexMeta`，实现多列索引需要元数据中支持多列信息的存储，以及它与 *Json* 格式数据的互相转换。

创建 *B+* 树的过程中，由于索引列从一个变为多个，*B+* 树的`IndexFileHeader`与`KeyComparator`都需要进行相应的改造。

向 *B+* 树中插入数据时，由于索引列的排列在行数据中不一定是自左向右有序排列的，因此`make_key`的过程也需要进行相应调整，其中涉及到的索引列偏移量等信息可以考虑存入 *B+* 树的`IndexFileHeader`。

##### 索引的使用

单列索引的情况下，一个索引只能对应一个列，因此查询时只需要简单的用列名去对比索引列名称，就可以确定有没有索引。支持多列索引以后，索引的使用条件也需要重新考虑。

##### 提示

多列索引实现过程中，可以简单考虑一下 *Unique* 应该如何实现。普通索引的维护过程中发生错误，外界可能是察觉不到的，但这些问题在 *Unique* 的实现过程中都会暴露出来。



## Show Index 实现

以 *show index from t1* 为例。*show index*与*desc table*在结构、功能上较为类似，因此实现过程可以参考 *desc table* 语句。

##### 词语法解析阶段

*show index*语句的语法结构如下：

```sql
SHOW INDEX FROM ID;
```

可以看到语句中需要存储的信息只有*table_name*，其对应的*SqlNode*结构如下：

```c++
struct ShowIndexSqlNode
{
  std::string relation_name;  ///< Relation name
};
```

然后还需要在`SqlCommandFlag`中增加一种对应的类型`SCF_SHOW_INDEX`，在在`ParsedSqlNode`中增加`ShowIndexSqlNode`，以及在语法文件中增加*show index*语句的语法规则。

```yacc
show_index_stmt:      /*show index 语句的语法解析树*/
    SHOW INDEX FROM ID
    {
      $$ = new ParsedSqlNode(SCF_SHOW_INDEX);
      $$->show_index.relation_name = $4;
      free($4);
    }
    ;
```

##### 语义解析阶段

本阶段要根据`ShowIndexSqlNode`生成对应的`Stmt`，我们需要增加一种`StmtType`，以及实现对应的`ShowIndexStmt`。这里可以参考`DescTableStmt`的实现

我们主要需要实现`ShowIndexStmt::create`方法。*show index* 结构较为简单，只需要确认`Table`存在，然后保存 *table_name* 即可。

##### 执行阶段

本阶段需要根据`ShowIndexStmt`中保存的 *table_name* 信息，获取`Table`中的所有`Index`信息。具体实现可以参考`DescTableExecutor`。执行结果分为表头和索引信息两个部分，表头信息存储在`TupleSchema`结构中，