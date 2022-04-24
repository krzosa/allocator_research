#include <stdio.h>
#include <stdint.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define function static
typedef uint64_t SizeU;
typedef int64_t  SizeI;
typedef uint8_t  U8;
typedef uint32_t U32;
typedef uint64_t U64;
typedef int8_t   S8;
typedef int32_t  S32;
typedef int64_t  S64;
typedef int8_t   B8;
typedef int32_t  B32;
typedef int64_t  B64;
#define kib(x) ((x)*1024llu)
#define mib(x) (kib(x)*1024llu)
#define gib(x) (mib(x)*1024llu)
#define assert(x) do{ if(!(x)) __debugbreak(); } while(0)
#define assert_msg(x,...) assert(x)
#define buff_cap(x) (sizeof((x))/sizeof((x)[0]))

constexpr SizeU page_size = 4096;
struct OS_Memory{
  U8 *p;
  SizeU commit;
  SizeU reserve;
};

function B32
is_power_of_2(SizeU x){
  SizeU result = (x & (x - 1)) == 0;
  return result;
}

function SizeU
get_align_offset(SizeU x, SizeU powerof2){
  assert(is_power_of_2(powerof2));
  SizeU not_pow2 = x & (powerof2 - 1);
  if(not_pow2){
    SizeU result = (powerof2 - not_pow2);
    return result;
  }
  return 0;
}

function SizeU
align_up(SizeU x, SizeU pow2){
  SizeU result = x + get_align_offset(x, pow2);
  return result;
}

function SizeU
clamp_top(SizeU val, SizeU top){
  if(val>top) return top;
  return val;
}

function SizeU
clamp_bot(SizeU bot, SizeU val){
  if(val<bot) return bot;
  return val;
}

function OS_Memory
os_reserve(SizeU size){
  OS_Memory result = {};
  result.reserve = align_up(size, page_size);
  result.p = (U8 *)VirtualAlloc(0, size, MEM_RESERVE, PAGE_READWRITE);
  assert_msg(result.p, "Failed to reserve memory");
  return result;
}

function void
os_commit(OS_Memory *m, SizeU size){
  size = align_up(size, page_size);
  SizeU new_commit = m->commit + size;
  new_commit = clamp_top(new_commit, m->reserve);
  SizeU commit_size = new_commit - m->commit;
  assert_msg(commit_size, "Run out of reserved memory");
  if(commit_size){
    void *p = VirtualAlloc(m->p + m->commit, commit_size, MEM_COMMIT, PAGE_READWRITE);
    assert_msg(p, "Failed to commit memory");
    m->commit = new_commit;
  }
}

function void
os_decommit(OS_Memory *m, SizeU size){
  size = align_up(size, page_size);
  size = clamp_top(size, m->commit);
  if(size){
    m->commit -= size;
    U8 *p = m->p + m->commit;
    B32 result = VirtualFree(p, size, MEM_DECOMMIT);
    assert_msg(result, "Failed to deallocate memory");
  }
}

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

struct Arena:Base_Allocator{
  Arena *next;
  OS_Memory memory;
  SizeU len;
  U32 alignment;
  U32 decommit_line;
  void *last_allocation;
};
function void *arena_allocator_proc(Base_Allocator *allocator, Allocation_Kind kind, SizeU size, void *old_pointer);

function void
arena_init(Arena *arena, SizeU reserve, U32 alignment, U32 decommit_line){
  arena->memory = os_reserve(reserve);
  arena->alignment = alignment;
  arena->proc = arena_allocator_proc;
  assert_msg((arena->decommit_line % page_size) == 0, "Should be a multiple of page size");
  arena->decommit_line = decommit_line;
  arena->len = 0;
}

constexpr SizeU default_reserve_size = gib(1);
constexpr SizeU default_alignment = 8;
constexpr SizeU default_decommit_line = mib(1);
constexpr SizeU default_commit_size = mib(128);
#define arena_push_array(a,T,c) (T*)arena_push_size((a), sizeof(T)*(c))
#define arena_push_struct(a,T) arena_push_array(a,T,1)

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

function void *
arena_push_size(Arena *arena, SizeU size){
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

function void *
arena_allocator_proc(Base_Allocator *allocator, Allocation_Kind kind, SizeU size, void *old_pointer){
  Arena *arena = (Arena *)allocator;
  switch(kind){
    case AK_Alloc:{
      return arena_push_size(arena, size);
    } break;
    case AK_Resize:{
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

function void
alloc_clear(Base_Allocator *alloc){
  alloc->proc(alloc, AK_Clear, 0, 0);
}

function void *
alloc_resize(Base_Allocator *alloc, void *p, SizeU new_size){
  return alloc->proc(alloc, AK_Resize, new_size, p);
}

#define alloc_array(a,T,c) (T *)allocate(a,sizeof(T)*(c))
function void *
allocate(Base_Allocator *alloc, SizeU size){
  return alloc->proc(alloc, AK_Alloc, size, 0);
}

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
  
  void clear(){
    // free_memory_block(allocator); // arena only
    //alloc_clear(allocator);
  }
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
      void *result = HeapAlloc(os_heap->heap, 0, size);
      assert(result);
      return result;
    } break;
    case AK_Resize:{
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
      return malloc(size);
    } break;
    case AK_Resize:{
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

int main(){
  for(SizeU i = 0; i < buff_cap(thread_ctx.blocks); i++){
    Arena *a = thread_ctx.blocks + i;
    arena_init(a, mib(128), 8, 0);
    a->next = thread_ctx.first_free;
    thread_ctx.first_free = a;
  }
  
  {
    Arena arena = {};
    arena_init(&arena, kib(8), 8, kib(4));
    int *a = arena_push_struct(&arena, int);
    *a = 10;
    assert(arena.memory.commit == kib(8));
    void *p = arena_push_size(&arena, kib(3));
    arena_push_size(&arena, kib(3));
    arena_clear(&arena);
    assert(arena.len == 0);
    assert(arena.decommit_line = kib(4));
    assert(arena.memory.commit = kib(4));
    // Leak arena~
  }
  
  constexpr int array_items = 1000000;
  {
    U64 begin = __rdtsc();
    Array<int> array = {};
    for(int i = 0; i < array_items; i++){
      array.push(i);
    }
    for(int i = 0; i < array_items; i++){
      assert(array[i] == i);
    }
    array.clear();
    U64 end = __rdtsc();
    printf("Arena time: %zu\n", end - begin);
  }
  
  
  OS_Heap_Allocator heap = {};
  heap.init();
  {
    U64 begin = __rdtsc();
    Array<int> array = {};
    array.allocator = &heap;
    for(int i = 0; i < array_items; i++){
      array.push(i);
    }
    for(int i = 0; i < array_items; i++){
      assert(array[i] == i);
    }
    array.clear();
    U64 end = __rdtsc();
    printf("OS Heap time: %zu\n", end - begin);
  }
  
  
  
  CRT_Malloc_Allocator crt;
  {
    U64 begin = __rdtsc();
    Array<int> array = {};
    array.allocator = &crt;
    for(int i = 0; i < array_items; i++){
      array.push(i);
    }
    for(int i = 0; i < array_items; i++){
      assert(array[i] == i);
    }
    array.clear();
    U64 end = __rdtsc();
    printf("CRT Malloc time: %zu\n", end - begin);
  }
}