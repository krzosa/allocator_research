
//-----------------------------------------------------------------------------
// Utilities
//-----------------------------------------------------------------------------
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


//-----------------------------------------------------------------------------
// OS Virtual memory operations
//-----------------------------------------------------------------------------
constexpr SizeU page_size = 4096;
struct OS_Memory{
  U8 *p;
  SizeU commit;
  SizeU reserve;
};

function B32
os_memory_initialized(OS_Memory *m){
  B32 result = m->p != 0;
  return result;
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

function void
os_release(OS_Memory *m){
  BOOL result = VirtualFree(m->p, 0, MEM_RELEASE);
  assert_msg(result, "Failed to release virtual memory");
  
  m->p = 0;
  m->reserve = 0;
  m->commit = 0;
}
