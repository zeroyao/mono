#include <mono/metadata/object.h>
#include <mono/metadata/object-internals.h>
#include <mono/metadata/metadata.h>
#include <mono/metadata/tabledefs.h>
#include <mono/metadata/class-internals.h>
#include <mono/metadata/domain-internals.h>

typedef void* (*MonoObjectGraphMalloc) (guint size);
typedef void (*MonoObjectGraphFree) (void* ptr);

typedef struct _LinearAllocator LinearAllocator;
typedef struct _MonoObjectGraph MonoObjectGraph;

#define LINEAR_ALLOCATOR_CHUNK_SIZE	(1024*10)

typedef struct _LinearAllocator
{
	guchar* buffer;
	guint totalSize;
	guint allocated;
	LinearAllocator* prev;
} LinearAllocator;

typedef struct _MonoObjectGraph
{
	MonoObjectGraphMalloc malloc;
	MonoObjectGraphFree free;
	LinearAllocator* allocator;

	MonoObject**	roots;
} MonoObjectGraph;

void mono_object_graph_add_allocator (MonoObjectGraph* objectGraph)
{
	LinearAllocator* allocator = objectGraph->malloc (sizeof(LinearAllocator));
	allocator->buffer = objectGraph->malloc (LINEAR_ALLOCATOR_CHUNK_SIZE);
	allocator->totalSize = LINEAR_ALLOCATOR_CHUNK_SIZE;
	allocator->allocated = 0;
	allocator->prev = objectGraph->allocator;
	objectGraph->allocator = allocator;
}

MonoObjectGraph* mono_object_graph_initialize (MonoObjectGraphMalloc malloc, MonoObjectGraphFree free)
{
	MonoObjectGraph* g = malloc (sizeof(MonoObjectGraph));
	memset (g, 0, sizeof(MonoObjectGraph));
	g->malloc = malloc;
	g->free = free;
	mono_object_graph_add_allocator (g);
	return g;
}

void mono_object_graph_cleanup (MonoObjectGraph* g)
{
	LinearAllocator* alloc = g->allocator;
	while (alloc != NULL)
	{
		g->free (alloc->buffer);
		alloc = alloc->prev;
	}
	g->free (g);
}

void* mono_object_graph_allocate (MonoObjectGraph* g, guint size, guint alignment)
{
	gsize next = (gsize)g->allocator->buffer + g->allocator->allocated;
	next = (next + alignment - 1) & ~(alignment - 1);
	if (next + size > (gsize)g->allocator->buffer + g->allocator->totalSize)
	{
		mono_object_graph_add_allocator (g);
		next = (gsize)g->allocator->buffer;
		next = (next + alignment - 1) & ~(alignment - 1);
		if (next + size > (gsize)g->allocator->buffer + g->allocator->totalSize)
			return NULL;
	}
	g->allocator->allocated = next + size - (gsize)g->allocator->buffer;
	return (void*)next;
}

#define LINEAR_ALLOC(type, size, g) (type*)mono_object_graph_allocate(g, (size), 8)

typedef struct _ObjectGraphNode ObjectGraphNode;
typedef struct _ObjectGraphEdge ObjectGraphEdge;
typedef struct _ValueField ValueField;

// graph's node, representing a reference i.e. MonoObject
struct _ObjectGraphNode
{
	MonoObject*			object;
	gboolean			isArray;
	ObjectGraphEdge*	edges;
	ValueField*			valueFields;
};

// graph's edge, representing a field of reference type
struct _ObjectGraphEdge
{
	ObjectGraphEdge*	next;
	const char*			name; // field name
	ObjectGraphNode*	reference;
};

// a value field
struct _ValueField
{
	ValueField* next;
	const char*	name; // field name
	MonoClass*	type;
};

//
//
MonoObjectGraph* mono_unity_object_graph_dump (MonoObjectGraphMalloc malloc, MonoObjectGraphFree free, gpointer* rootHandles, guint numRoots, guint memorySize)
{
	guint i;
	MonoObjectGraph* g = mono_object_graph_initialize(malloc, free);

	g->roots = LINEAR_ALLOC (MonoObject*, sizeof(MonoObject*)*numRoots, g);
	for (i = 0; i < numRoots; ++i)
		g->roots[i] = mono_gchandle_get_target (GPOINTER_TO_UINT (rootHandles[i]));

	return g;
}

void mono_unity_object_graph_cleanup (MonoObjectGraph* objectGraph)
{
	mono_object_graph_cleanup (objectGraph);
}
