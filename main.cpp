#include "types.cpp"
#include "os.cpp"

//-----------------------------------------------------------------------------
// Benchmark tool
//-----------------------------------------------------------------------------
global struct Test_Time{
  const char *name;
  U64 time;
  U64 count;
} global_test[16];

struct Benchmark_Scope{
  U64 begin;
  const char *name;
  U64 i;
  Benchmark_Scope(U64 i, const char *name){
    begin = __rdtsc();
    this->i = i;
    this->name = name;
  }
  ~Benchmark_Scope(){
    global_test[i].time += __rdtsc() - begin;
    global_test[i].name = name;
    global_test[i].count++;
  }
};

//-----------------------------------------------------------------------------
// Base Allocator
//-----------------------------------------------------------------------------
enum Allocation_Kind{
  AK_Alloc,
  AK_Free,
  AK_Resize,
  AK_Clear,
};

struct Base_Allocator;
typedef void *Allocator_Proc(Base_Allocator *, Allocation_Kind, SizeU, void *old_pointer);
struct Base_Allocator{
  Allocator_Proc *proc;
};

//-----------------------------------------------------------------------------
// Arena allocator
//-----------------------------------------------------------------------------
struct Arena:Base_Allocator{
  Arena *next;
  OS_Memory memory;
  SizeU len;
  U32 alignment;
  U32 decommit_line;
  void *last_allocation;
};
function void *arena_allocator_proc(Base_Allocator *allocator, Allocation_Kind kind, SizeU size, void *old_pointer);

//-----------------------------------------------------------------------------
// Arena allocator
//-----------------------------------------------------------------------------
constexpr SizeU default_reserve_size = gib(1);
constexpr SizeU default_alignment = 8;
constexpr SizeU default_decommit_line = mib(1);
constexpr SizeU default_commit_size = mib(1);

function void
arena_init(Arena *arena, SizeU reserve, U32 alignment, U32 decommit_line){
  arena->memory = os_reserve(reserve);
  arena->alignment = alignment;
  arena->proc = arena_allocator_proc;
  assert_msg((arena->decommit_line % page_size) == 0, "Should be a multiple of page size");
  arena->decommit_line = decommit_line;
  arena->len = 0;
}

function void
arena_try_resizing(Arena *arena, SizeU size){
  SizeU possible_size = size + arena->alignment;
  if(arena->len + possible_size > arena->memory.commit){
    if(!arena->memory.reserve){
      arena_init(arena, default_reserve_size, default_alignment, default_decommit_line);
    }
    os_commit(&arena->memory, possible_size+default_commit_size);
  }
}

#define arena_push_array(a,T,c) (T*)arena_push_size((a), sizeof(T)*(c))
#define arena_push_struct(a,T) arena_push_array(a,T,1)
function void *arena_push_size(Arena *arena, SizeU size){
  arena_try_resizing(arena, size);
  assert(arena->len + size < arena->memory.reserve);
  arena->len += get_align_offset((SizeU)arena->memory.p, arena->alignment);
  arena->last_allocation = arena->memory.p + arena->len;
  arena->len += size;
  return arena->last_allocation;
}

function void
arena_pop_to_pos(Arena *arena, SizeU pos){
  if(arena->len > arena->decommit_line &&
     pos < arena->decommit_line){
    SizeU diff = arena->memory.commit - arena->decommit_line;
    os_decommit(&arena->memory, diff);
  }
  arena->len = pos;
  arena->last_allocation = 0;
}

function void
arena_clear(Arena *arena){
  arena_pop_to_pos(arena, 0);
}

function void
alloc_clear(Base_Allocator *alloc){
  alloc->proc(alloc, AK_Clear, 0, 0);
}

function void
alloc_free(Base_Allocator *alloc, void *pointer){
  alloc->proc(alloc, AK_Free, 0, pointer);
}

function void *
alloc_resize(Base_Allocator *alloc, void *p, SizeU new_size){
  return alloc->proc(alloc, AK_Resize, new_size, p);
}

#define alloc_array(a,T,c) (T *)allocate(a,sizeof(T)*(c))
function void *allocate(Base_Allocator *alloc, SizeU size){
  return alloc->proc(alloc, AK_Alloc, size, 0);
}

function void *
arena_allocator_proc(Base_Allocator *allocator, Allocation_Kind kind, SizeU size, void *old_pointer){
  Arena *arena = (Arena *)allocator;
  switch(kind){
    case AK_Alloc:{
      Benchmark_Scope(0, "arena_push_size");
      return arena_push_size(arena, size);
    } break;
    case AK_Resize:{
      Benchmark_Scope(1, "arena_push_resize");
      if(old_pointer == arena->last_allocation){
        SizeU arena_base = (SizeU)(arena->memory.p + arena->len);
        SizeU allocation_address = (SizeU)arena->last_allocation;
        assert(arena_base > allocation_address);
        SizeU last_allocation_size =  arena_base - allocation_address;
        SizeU additional_size = size - last_allocation_size;
        arena_try_resizing(arena, additional_size);
        assert_msg(arena->len + additional_size < arena->memory.reserve, "Not enough space in arena");
        arena->len += additional_size;
        return arena->last_allocation;
      }
      assert_msg(0, "Resizing something that wasn't allocated recently on arena");
      return 0;
    } break;
    case AK_Clear:{
      arena_clear(arena);
      return 0;
    } break;
    case AK_Free:{
      assert_msg(0, "No freeing under this allocators watch");
      return 0;
    } break;
  }
}

//-----------------------------------------------------------------------------
// Thread local block fetcher
//-----------------------------------------------------------------------------
struct ThreadCtx{
  Arena blocks[128];
  Arena *first_free;
};
thread_local ThreadCtx thread_ctx;

function Base_Allocator *
get_memory_block(){
  assert_msg(thread_ctx.first_free,"No free memory blocks on arena");
  Arena *free = thread_ctx.first_free;
  thread_ctx.first_free = thread_ctx.first_free->next;
  if(!os_memory_initialized(&free->memory)){
    arena_init(free, mib(128), 8, 0);
  }
  return free;
}

function void
free_memory_block(Base_Allocator *a){
  Arena *arena = (Arena *)a;
  arena_clear(arena);
  arena->next = thread_ctx.first_free;
  thread_ctx.first_free = arena;
}

function SizeU
max(SizeU a, SizeU b){
  if(a>b)return a;
  return b;
}

//-----------------------------------------------------------------------------
// Array
//-----------------------------------------------------------------------------
template<class T>
struct Array{
  Base_Allocator *allocator;
  T *data;
  SizeU cap, len;
  
  T *alloc(SizeU size = 1){
    if(cap == 0){
      if(!allocator) allocator = get_memory_block();
      cap = max(size*2, 16);
      data = alloc_array(allocator, T, cap);
    }
    else if(len+size>cap){
      cap = (len+size)*2;
      data = (T *)alloc_resize(allocator, data, sizeof(T)*cap);
    }
    T *result = data + len;
    len += size;
    return result;
  }
  
  void push(T &e){
    T *slot = alloc();
    *slot = e;
  }
  
  void clear(){free_memory_block(allocator);}
  T &operator[](SizeU i){return data[i];}
  T *begin(){return data;}
  T *end(){return data+len;}
};

//-----------------------------------------------------------------------------
// Windows Heap API
//-----------------------------------------------------------------------------
function void *
os_heap_allocator_proc(Base_Allocator *allocator, Allocation_Kind kind, SizeU size, void *old_pointer);
struct OS_Heap_Allocator:Base_Allocator{
  HANDLE heap;
  
  void init(){
    proc = os_heap_allocator_proc;
    heap = GetProcessHeap();HeapCreate(0, 0, gib(2));
    assert(heap);
  }
  
};

function void *
os_heap_allocator_proc(Base_Allocator *allocator, Allocation_Kind kind, SizeU size, void *old_pointer){
  auto *os_heap = (OS_Heap_Allocator *)allocator;
  switch(kind){
    case AK_Alloc:{
      Benchmark_Scope(2, "heap_alloc");
      void *result = HeapAlloc(os_heap->heap, 0, size);
      assert(result);
      return result;
    } break;
    case AK_Resize:{
      Benchmark_Scope(3, "heap_realloc");
      void *result = HeapReAlloc(os_heap->heap, 0, old_pointer, size);
      assert(result);
      return result;
    } break;
    case AK_Clear:{
      assert_msg(0, "No clear");
      return 0;
    } break;
    case AK_Free:{
      HeapFree(os_heap->heap, 0, old_pointer);
      return 0;
    } break;
  }
}

//-----------------------------------------------------------------------------
// Windows CRT Malloc
//-----------------------------------------------------------------------------
#include <stdlib.h>
function void *
crt_malloc_allocator_proc(Base_Allocator *allocator, Allocation_Kind kind, SizeU size, void *old_pointer);
struct CRT_Malloc_Allocator:Base_Allocator{
  CRT_Malloc_Allocator(){
    proc = crt_malloc_allocator_proc;
  }
};

function void *
crt_malloc_allocator_proc(Base_Allocator *allocator, Allocation_Kind kind, SizeU size, void *old_pointer){
  switch(kind){
    case AK_Alloc:{
      Benchmark_Scope(4, "malloc");
      return malloc(size);
    } break;
    case AK_Resize:{
      Benchmark_Scope(5, "realloc");
      return realloc(old_pointer, size);
    } break;
    case AK_Clear:{
      assert_msg(0, "No clear");
      return 0;
    } break;
    case AK_Free:{
      free(old_pointer);
      return 0;
    } break;
  }
}

#define ASSERTS 1
int main(){
  for(SizeU i = 0; i < buff_cap(thread_ctx.blocks); i++){
    Arena *a = thread_ctx.blocks + i;
    a->next = thread_ctx.first_free;
    thread_ctx.first_free = a;
  }
  
  constexpr int iters = 1000;
  constexpr int array_items = 10000;
  for(int i = 0; i < iters; i++){
    Benchmark_Scope scope(8, "Arena array");
    Array<int> array = {};
    for(int i = 0; i < array_items; i++){
      array.push(i);
    }
#if ASSERTS
    for(int i = 0; i < array_items; i++){
      assert(array[i] == i);
    }
#endif
    array.clear();
  }
  
  OS_Heap_Allocator heap = {};
  heap.init();
  for(int i = 0; i < iters; i++){
    Benchmark_Scope scope(7, "OS Heap array");
    Array<int> array = {};
    array.allocator = &heap;
    for(int i = 0; i < array_items; i++){
      array.push(i);
    }
#if ASSERTS
    for(int i = 0; i < array_items; i++){
      assert(array[i] == i);
    }
#endif
    //array.clear();
    alloc_free(&heap, array.data);
  }
  
  CRT_Malloc_Allocator crt;
  for(int i = 0; i < iters; i++){
    Benchmark_Scope scope(6, "CRT Array");
    Array<int> array = {};
    array.allocator = &crt;
    for(int i = 0; i < array_items; i++){
      array.push(i);
    }
#if ASSERTS
    for(int i = 0; i < array_items; i++){
      assert(array[i] == i);
    }
#endif
    //array.clear();
    alloc_free(&crt, array.data);
  }
  
  printf("====== Arena stats =======\n");
  printf("default_reserve_size %zu\n", default_reserve_size);
  printf("default_alignment %zu\n", default_alignment);
  printf("default_decommit_line %zu\n", default_decommit_line);
  printf("default_commit_size %zu\n", default_commit_size);
  
  printf("\n");
  for(int i = 0; i < 9; i++){
    U64 t = global_test[i].time / global_test[i].count;
    printf("Name: %s Hits: %zu TotalTime: %zu AverageTime: %zu\n",
           global_test[i].name, global_test[i].count,
           global_test[i].time, t);
  }
  getc(stdin);
}