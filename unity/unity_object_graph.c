#include <mono/metadata/object.h>
#include <mono/metadata/object-internals.h>
#include <mono/metadata/metadata.h>
#include <mono/metadata/tabledefs.h>
#include <mono/metadata/class-internals.h>
#include <mono/metadata/domain-internals.h>
#include "unity_object_graph.h"

//////////////////////////////////////////////////////////////////////////////

typedef struct _TraverseContext TraverseContext;

struct _TraverseContext
{
	MonoObjectGraph* g;
	QueuedNode* queueBegin;
	QueuedNode* queueEnd;
};

//////////////////////////////////////////////////////////////////////////////
// Memory management

void mono_object_graph_initialize_allocator (MonoObjectGraph* objectGraph, guchar* buffer, guint32 bufferSize)
{
	LinearAllocator* allocator = &objectGraph->allocator;
	allocator->buffer = buffer;
	allocator->totalSize = bufferSize;
	allocator->allocated = 0;
}

void* mono_object_graph_allocate (MonoObjectGraph* g, guint32 size, guint32 alignment)
{
	gsize next = (gsize)g->allocator.buffer + g->allocator.allocated;
	next = (next + alignment - 1) & ~(alignment - 1);
	if (next + size > (gsize)g->allocator.buffer + g->allocator.totalSize)
	{
		g_assert_not_reached ();
		return NULL;
	}
	g->allocator.allocated = next + size - (gsize)g->allocator.buffer;
	return (void*)next;
}

#define LINEAR_ALLOC(type, size, g) (type*)mono_object_graph_allocate(g, (size), 8)

MonoObjectGraph* mono_object_graph_initialize (gpointer buffer, guint32 bufferSize)
{
	MonoObjectGraph* g = (MonoObjectGraph*)(((gsize)buffer + 7) & ~7);
	g_assert (((gsize)(g + 1) - (gsize)buffer) <= bufferSize);

	memset (g, 0, sizeof(MonoObjectGraph));
	mono_object_graph_initialize_allocator (g, (guchar*)(g + 1), (gsize)buffer + bufferSize - (gsize)(g + 1));
	return g;
}

void mono_object_graph_cleanup (MonoObjectGraph* g)
{
}

//////////////////////////////////////////////////////////////////////////////

#define MARK_OBJ(obj) do { (obj)->vtable = (MonoVTable*)(((gsize)(obj)->vtable) | (gsize)1); } while (0)
#define UNMARK_OBJ(obj) do { (obj)->vtable = (MonoVTable*)(((gsize)(obj)->vtable) & ~(gsize)1); } while (0)
#define IS_MARKED(obj) (((gsize)(obj)->vtable) & (gsize)1)
#define GET_VTABLE(obj) ((MonoVTable*)(((gsize)(obj)->vtable) & ~(gsize)1))

void mono_object_graph_enque (ObjectGraphNode* node, TraverseContext* ctx)
{
	QueuedNode* qnode;

	g_assert (node != NULL);

	qnode = LINEAR_ALLOC (QueuedNode, sizeof(QueuedNode), ctx->g);
	qnode->next = NULL;
	qnode->node = node;

	if (ctx->queueBegin == NULL)
	{
		g_assert (ctx->queueEnd == NULL);
		ctx->queueBegin = ctx->queueEnd = qnode;
	}
	else
	{
		ctx->queueEnd->next = qnode;
		ctx->queueEnd = qnode;
	}
}

ObjectGraphNode* mono_object_graph_deque (TraverseContext* ctx)
{
	QueuedNode* ret;

	if (ctx->queueBegin == NULL)
	{
		g_assert (ctx->queueEnd == NULL);
		return NULL;
	}

	ret = ctx->queueBegin;
	ctx->queueBegin = ret->next;
	if (ctx->queueBegin == NULL)
		ctx->queueEnd = NULL;

	return ret->node;
}

ObjectGraphNode* mono_object_graph_map (MonoObject* object, gboolean isReference, MonoClass* klass, TraverseContext* ctx)
{
	QueuedNode* qnode;
	ObjectGraphNode* node;
	MonoObjectGraph* g = ctx->g;

	g_assert (object != NULL);
	g_assert (!isReference || klass == NULL);

	if (isReference && IS_MARKED (object))
	{
		for (qnode = g->allNodesBegin; qnode != NULL; qnode = qnode->next)
		{
			if (qnode->node->object == object)
				return qnode->node;
		}
		g_assert_not_reached ();
	}

	qnode = LINEAR_ALLOC (QueuedNode, sizeof(QueuedNode), g);
	qnode->next = NULL;
	qnode->node = node = LINEAR_ALLOC (ObjectGraphNode, sizeof(ObjectGraphNode), g);
	memset (node, 0, sizeof(ObjectGraphNode));
	node->index = g->numAllNodes++;
	node->object = object;
	node->klass = isReference ? GET_VTABLE(object)->klass : klass ;
	node->type = isReference ? Type_Reference : Type_Value;
	node->typeName = node->klass->name;
	node->typeSize = node->klass->instance_size;

	if (!isReference || !IS_MARKED (object))
		mono_object_graph_enque (qnode->node, ctx);

	if (isReference)
		MARK_OBJ (object);

	if (g->allNodesBegin == NULL)
	{
		g_assert (g->allNodesEnd == NULL);
		g->allNodesBegin = g->allNodesEnd = qnode;
	}
	else
	{
		g->allNodesEnd->next = qnode;
		g->allNodesEnd = qnode;
	}

	return node;
}

//////////////////////////////////////////////////////////////////////////////

extern void GC_stop_world_external ();
extern void GC_start_world_external ();
static void mono_object_graph_traverse_generic	(ObjectGraphNode* node, TraverseContext* ctx);
static void mono_object_graph_traverse_array (ObjectGraphNode* node, TraverseContext* ctx);
static void mono_object_graph_traverse_object (ObjectGraphNode* node, TraverseContext* ctx);

//////////////////////////////////////////////////////////////////////////////
// Public interface

MonoObjectGraph* mono_unity_object_graph_dump (MonoObject** rootObjects, guint32 numRoots, gpointer buffer, guint32 bufferSize)
{
	guint32 i;
	ObjectGraphNode* node;
	TraverseContext ctx;
	MonoObjectGraph* g;
	QueuedNode* qnode;

	GC_stop_world_external ();

	g = mono_object_graph_initialize(buffer, bufferSize);
	g->roots = LINEAR_ALLOC (MonoObject*, sizeof(MonoObject*) * numRoots, g);
	memcpy (g->roots, rootObjects, sizeof(MonoObject*) * numRoots);
	g->rootNodes = LINEAR_ALLOC (ObjectGraphNode*, sizeof(ObjectGraphNode*) * numRoots, g);
	memset (g->rootNodes, 0, sizeof(ObjectGraphNode*) * numRoots);
	g->numRoots = numRoots;

	// enqueue roots
	memset (&ctx, 0, sizeof(TraverseContext));
	ctx.g = g;
	for (i = 0; i < numRoots; ++i)
		g->rootNodes[i] = mono_object_graph_map (g->roots[i], TRUE, NULL, &ctx);

	while ((node = mono_object_graph_deque (&ctx)) != NULL)
	{
		mono_object_graph_traverse_generic (node, &ctx);
	}

	// clear marks
	qnode = g->allNodesBegin;
	while (qnode != NULL)
	{
		if (qnode->node->type == Type_Reference)
		{
			g_assert (qnode->node->klass == GET_VTABLE(qnode->node->object)->klass);
			UNMARK_OBJ (qnode->node->object);
		}
		qnode = qnode->next;
	}

	GC_start_world_external ();

	return g;
}

void mono_unity_object_graph_cleanup (MonoObjectGraph* objectGraph)
{
	mono_object_graph_cleanup (objectGraph);
}

//////////////////////////////////////////////////////////////////////////////
// Implementations

static void mono_object_graph_traverse_generic (ObjectGraphNode* node, TraverseContext* ctx)
{
	if (node->klass->rank)
		mono_object_graph_traverse_array (node, ctx);
	else
		mono_object_graph_traverse_object (node, ctx);
}

static void mono_object_graph_traverse_array (ObjectGraphNode* node, TraverseContext* ctx)
{
	MonoArray* array = (MonoArray*)node->object;
	MonoObject* object = node->object;
	MonoClass* klass = node->klass;

	MonoClass* elementClass;
	gboolean isStruct;
	gboolean isReference;

	mono_array_size_t arrayLength, i;
	gint32 elementClassSize;
	MonoObject* elementObject;

	g_assert (object);
	elementClass = klass->element_class;
	isStruct = MONO_TYPE_ISSTRUCT (&elementClass->byval_arg);
	isReference = MONO_TYPE_IS_REFERENCE (&elementClass->byval_arg);
	g_assert (elementClass->size_inited != 0);

	g_assert (node->type == Type_Reference);
	node->isArray = TRUE;

	arrayLength = mono_array_length (array);

	node->fields = LINEAR_ALLOC (ObjectGraphField, sizeof(ObjectGraphField) * arrayLength, ctx->g);
	node->numFields = arrayLength;
	memset (node->fields, 0, sizeof(ObjectGraphField) * arrayLength);

	for (i = 0; i < arrayLength; ++i)
	{
		elementObject = NULL;
		if (isStruct)
		{
			elementClassSize = mono_class_array_element_size (elementClass);
			elementObject = (MonoObject*)mono_array_addr_with_size (array, elementClassSize, i);
			// subtract the added offset for the vtable. This is added to the offset even though it is a struct
			--elementObject;
		}
		else if (isReference)
		{
			elementObject = mono_array_get (array, MonoObject*, i);
		}

		node->fields[i].name = (const char*)i;
		node->fields[i].type = isReference ? Type_Reference : Type_Value;
		node->fields[i].typeName = mono_type_get_name (&elementClass->byval_arg);
		if (elementObject != NULL)
			node->fields[i].otherNode = mono_object_graph_map (elementObject, isReference, isReference ? NULL : elementClass, ctx);
	}
}

static void mono_object_graph_traverse_object (ObjectGraphNode* node, TraverseContext* ctx)
{
	MonoObject* object = node->object;
	MonoClass* klass = node->klass;
	MonoClass* k;

	guint32 i;
	guint32 fieldCount, staticFieldCount;
	MonoClassField* field;
	ObjectGraphNode* fieldNode;
	ObjectGraphField* gfield;

	g_assert (object);

	fieldCount = staticFieldCount = 0;
	for (k = klass; k != NULL; k = k->parent)
	{
		if (k->size_inited == 0)
			continue;
		for (i = 0; i < k->field.count; ++i)
		{
			if (k->fields[i].type->attrs & FIELD_ATTRIBUTE_STATIC)
				++staticFieldCount;
			else
				++fieldCount;
		}
	}
	node->fields = fieldCount != 0 ? LINEAR_ALLOC (ObjectGraphField, sizeof(ObjectGraphField) * fieldCount, ctx->g) : NULL;
	node->numFields = fieldCount;
	node->staticFields = staticFieldCount != 0 ? LINEAR_ALLOC (ObjectGraphField, sizeof(ObjectGraphField) * staticFieldCount, ctx->g) : NULL;
	node->numStaticFields = staticFieldCount;

	fieldCount = staticFieldCount = 0;
	for (k = klass; k != NULL; k = k->parent)
	{
		if (k->size_inited == 0)
			continue;

		for (i = 0; i < k->field.count; ++i)
		{
			field = &k->fields[i];
			fieldNode = NULL;

			if (MONO_TYPE_ISSTRUCT (field->type))
			{
				gchar* offseted = (gchar*)object;
				offseted += field->offset;
				if (field->type->type == MONO_TYPE_GENERICINST)
				{
					g_assert (field->type->data.generic_class->cached_class);
					fieldNode = mono_object_graph_map ((MonoObject*)offseted - 1, FALSE, field->type->data.generic_class->cached_class, ctx);
				}
				else
					fieldNode = mono_object_graph_map ((MonoObject*)offseted - 1, FALSE, field->type->data.klass, ctx);
			}
			else
			{
				g_assert (field->offset != -1);

				if (MONO_TYPE_IS_REFERENCE(field->type))
				{
					MonoObject* val = NULL;
					mono_field_get_value (object, field, &val);
					if (val != NULL)
						fieldNode = mono_object_graph_map (val, TRUE, NULL, ctx);
				}
			}

			gfield = field->type->attrs & FIELD_ATTRIBUTE_STATIC
					 ? &node->staticFields[staticFieldCount++]
					 : &node->fields[fieldCount++];
			gfield->name = field->name;
			gfield->type = MONO_TYPE_IS_REFERENCE (field->type) ? Type_Reference : Type_Value;
			gfield->typeName = mono_type_get_name (field->type);
			gfield->otherNode = fieldNode;
		}
	}
}
