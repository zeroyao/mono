#ifndef _MONO_UNITY_OBJECT_GRAPH_H_
#define _MONO_UNITY_OBJECT_GRAPH_H_

typedef struct _LinearAllocator LinearAllocator;
typedef struct _MonoObjectGraph MonoObjectGraph;
typedef struct _ObjectGraphNode ObjectGraphNode;
typedef struct _ObjectGraphField ObjectGraphField;
typedef struct _QueuedNode QueuedNode;

typedef struct _LinearAllocator
{
	guchar* buffer;
	guint32 totalSize;
	guint32 allocated;
} LinearAllocator;

// graph's node, representing a reference i.e. MonoObject
struct _ObjectGraphNode
{
	guint32				index;
	MonoObject*			object;
	MonoClass*			klass;
	guint16				isArray;
	guint16				type;
	const char*			typeName;
	guint32				typeSize;
	ObjectGraphField*	fields;
	guint32				numFields;
	ObjectGraphField*	staticFields;
	guint32				numStaticFields;
};

// graph's edge, representing a field
struct _ObjectGraphField
{
	const char*			name;
	guint32				type;
	const char*			typeName;
	ObjectGraphNode*	otherNode;
};

struct _QueuedNode
{
	ObjectGraphNode* node;
	QueuedNode* next;
};

struct _MonoObjectGraph
{
	LinearAllocator		allocator;

	MonoObject**		roots;
	ObjectGraphNode**	rootNodes;
	guint32				numRoots;

	QueuedNode*			allNodesBegin;
	QueuedNode*			allNodesEnd;
	guint32				numAllNodes;
};

enum
{
	Type_Reference	= 0,
	Type_Value		= 1
};

#endif
