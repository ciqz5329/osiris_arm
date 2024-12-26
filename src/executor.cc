// Copyright 2021 Daniel Weber, Ahmad Ibrahim, Hamed Nemati, Michael Schwarz, Christian Rossow
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
//     limitations under the License.


#include "executor.h"

#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <sstream>

#include "code_generator.h"
#include "logger.h"

namespace osiris {

Executor::Executor() {
  // allocate memory for memory accesses during execution
  for (size_t i = 0; i < execution_data_pages_.size(); i++) {
    void* addr = reinterpret_cast<void*>(kMemoryBegin + i * kPagesize);
    // check that page is not mapped
    int ret = msync(addr, kPagesize, 0);
    if (ret != -1 || errno != ENOMEM) {
      LOG_ERROR("Execution page is already mapped. Aborting!");
      std::exit(1);
    }
    char* page = static_cast<char*>(mmap(addr,
                                         kPagesize,
                                         PROT_READ | PROT_WRITE,
                                         MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                                         -1,
                                         0));

    if (page != reinterpret_cast<void*>(kMemoryBegin + i* kPagesize) || page == MAP_FAILED) {
      LOG_ERROR("Couldn't allocate memory for execution (data memory). Aborting!");
      std::exit(1);
    }
    execution_data_pages_[i] = page;
  }

  // allocate memory that holds the actual instructions we execute
  for (size_t i = 0; i < execution_code_pages_.size(); i++) {
    execution_code_pages_[i] = static_cast<char*>(mmap(nullptr,
                                                       kPagesize,
                                                       PROT_READ | PROT_WRITE | PROT_EXEC,
                                                       MAP_PRIVATE | MAP_ANONYMOUS,
                                                       -1,
                                                       0));
    if (execution_code_pages_[i] == MAP_FAILED) {
      LOG_ERROR("Couldn't allocate memory for execution (exec memory). Aborting!");
      std::exit(1);
    }
  }

#if DEBUGMODE == 0
  // if we are not in DEBUGMODE this will instead be inlined in Executor::ExecuteCodePage()
  std::cout << "DEBUGMODE ==0";
  std::array<int, 4> signals_to_handle = {SIGSEGV, SIGILL, SIGFPE, SIGTRAP};
  // register fault handler
  RegisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
#endif
}

Executor::~Executor()
{
#if DEBUGMODE == 0
  // if we are not in DEBUGMODE this will instead be inlined in Executor::ExecuteCodePage()
  std::array<int, 4> signals_to_handle = {SIGSEGV, SIGILL, SIGFPE, SIGTRAP};
  UnregisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
#endif
}

int Executor::TestResetSequence(const byte_array& trigger_sequence,
                                const byte_array& measurement_sequence,
                                const byte_array& reset_sequence,
                                int no_testruns,
                                int reset_executions_amount,
                                int64_t* cycles_difference) {
  byte_array nop_sequence = CreateSequenceOfNOPs(reset_sequence.size());//生成nop序列
  std::vector<int64_t> clean_runs;
  std::vector<int64_t> noisy_runs;
  clean_runs.reserve(no_testruns);//预留no_testruns个空间
  noisy_runs.reserve(no_testruns);

  // intuition:
  //  given a valid measure and trigger sequence:
  //  when reset;measure == trigger;reset;measure (or very small diff) -> reset sequence works
  CreateResetTestrunCode(0, nop_sequence, measurement_sequence, reset_sequence,
                         reset_executions_amount);
  CreateResetTestrunCode(1, trigger_sequence, measurement_sequence, reset_sequence,
                         reset_executions_amount);
  for (int i = 0; i < no_testruns; i++) {
    // get timing with reset sequence
    uint64_t cycles_elapsed_reset_measure;
    int error = ExecuteTestrun(0, &cycles_elapsed_reset_measure);
    if (error) {
      // abort
      *cycles_difference = -1;
      return 1;
    }
    clean_runs.push_back(cycles_elapsed_reset_measure);
  }

  for (int i = 0; i < no_testruns; i++) {
    // get timing without reset sequence
    uint64_t cycles_elapsed_trigger_reset_measure;
    int error = ExecuteTestrun(1, &cycles_elapsed_trigger_reset_measure);
    if (error) {
      // abort
      *cycles_difference = -1;
      return 1;
    }
    noisy_runs.push_back(cycles_elapsed_trigger_reset_measure);
  }
  *cycles_difference = static_cast<int64_t>(median<int64_t>(clean_runs) -
      median<int64_t>(noisy_runs));
  return 0;
}

int Executor::TestSequenceTriple(const byte_array& trigger_sequence,
                                 const byte_array& measurement_sequence,
                                 const byte_array& reset_sequence,
                                 int no_testruns,
                                 int64_t* cycles_difference) {
  std::vector<int64_t> results;
  CreateTestrunCode(0, trigger_sequence, reset_sequence, measurement_sequence, 1);
  CreateTestrunCode(1, reset_sequence, trigger_sequence, measurement_sequence, 1);
  for (int i = 0; i < no_testruns; i++) {
    // get timing for first experiment
    uint64_t cycles_elapsed_trigger_reset;
    int error = ExecuteTestrun(0, &cycles_elapsed_trigger_reset);
    if (error) {
      // abort
      *cycles_difference = -1;
      return 1;
    }

    // get timing for second experiment
    uint64_t cycles_elapsed_reset_trigger;
    error = ExecuteTestrun(1, &cycles_elapsed_reset_trigger);
    if (error) {
      // abort
      *cycles_difference = -1;
      return 1;
    }
    int64_t cycles_difference_per_run = static_cast<int64_t>(cycles_elapsed_trigger_reset) -
        static_cast<int64_t>(cycles_elapsed_reset_trigger);
    results.push_back(cycles_difference_per_run);
  }
  *cycles_difference = static_cast<int64_t>(median<int64_t>(results));
  return 0;
}
//成功返回0
int Executor::TestTriggerSequence(const byte_array& trigger_sequence,
                                  const byte_array& measurement_sequence,
                                  const byte_array& reset_sequence,
                                  bool execute_trigger_only_in_speculation,
                                  int no_testruns,
                                  int reset_executions_amount,
                                  int64_t* cycles_difference) {
  // disabled for performance reasons (on 2020-09-03 by Osiris dev)
  // can be enabled again without losing too much performance
  byte_array nop_sequence;// = CreateSequenceOfNOPs(trigger_sequence.size());

  // vectors are preallocated and just get cleared on everyrun for performance
  results_trigger.clear();
  results_notrigger.clear();
  results_trigger.reserve(no_testruns);
  results_notrigger.reserve(no_testruns);


  if (execute_trigger_only_in_speculation) {
    CreateSpeculativeTriggerTestrunCode(0, measurement_sequence,
                                        trigger_sequence,
                                        reset_sequence, reset_executions_amount);
    CreateSpeculativeTriggerTestrunCode(1, measurement_sequence,
                                        nop_sequence,
                                        reset_sequence, reset_executions_amount);
  } else {

    CreateTestrunCode(0, reset_sequence, trigger_sequence, measurement_sequence,
                      reset_executions_amount);

     CreateTestrunCode(1, reset_sequence, nop_sequence, measurement_sequence,
                       reset_executions_amount);


  }


  // get timing with trigger sequence
  for (int i = 0; i < no_testruns; i++) {

    uint64_t cycles_elapsed_trigger ;
    int error = ExecuteTestrun(0, &cycles_elapsed_trigger);
    if (error) {
      // abort
      *cycles_difference = -1;
      return 1;
    }
    if (cycles_elapsed_trigger <= 5000000000) {
      results_trigger.emplace_back(cycles_elapsed_trigger);
    }

  }



  //
  for (int i = 0; i < no_testruns; i++) {
    // get timing without trigger sequence
    uint64_t cycles_elapsed_notrigger ;

    int error = ExecuteTestrun(0, &cycles_elapsed_notrigger);
    if (error) {
      // abort
      *cycles_difference = -1;
      return 1;
    }
    if (cycles_elapsed_notrigger <= 500) {
      results_notrigger.emplace_back(cycles_elapsed_notrigger);
    }

  }
  double median_trigger = median<int64_t>(results_trigger);
  double median_notrigger = median<int64_t>(results_notrigger);
  *cycles_difference = static_cast<int64_t>(median_notrigger - median_trigger);

  return 0;
}

void Executor::CreateResetTestrunCode(int codepage_no, const byte_array& trigger_sequence,
                                      const byte_array& measurement_sequence,
                                      const byte_array& reset_sequence,
                                      int reset_executions_amount) {
  ClearDataPage();
  InitializeCodePage(codepage_no);

  // prolog
  AddProlog(codepage_no);
  AddInstructionToCodePage(codepage_no, trigger_sequence);
  AddSerializeInstructionToCodePage(codepage_no);

  // try to reset microarchitectural state again
  assert(reset_executions_amount <= 100);  // else we need to increase guardian stack space
  for (int i = 0; i < reset_executions_amount; i++) {
    AddInstructionToCodePage(codepage_no, reset_sequence);
  }
  AddSerializeInstructionToCodePage(codepage_no);

  // time measurement sequence
  AddTimerStartToCodePage(codepage_no);
  AddInstructionToCodePage(codepage_no, measurement_sequence);
  AddTimerEndToCodePage(codepage_no);

  // return timing result and epilog
  MakeTimerResultReturnValue(codepage_no);
  AddEpilog(codepage_no);

  // make sure that we do not exceed page boundaries
  assert(code_pages_last_written_index_[codepage_no] < kPagesize);
}

void Executor::CreateTestrunCode(int codepage_no, const byte_array& first_sequence,
                                 const byte_array& second_sequence,
                                 const byte_array& measurement_sequence,
                                 int first_sequence_executions_amount) {
  ClearDataPage();
  InitializeCodePage(codepage_no);

  // prolog
  //AddProlog(codepage_no);
  //AddSerializeInstructionToCodePage(codepage_no);

  // first sequence
  // if we need more we also have to increase the guardian stack space

  // assert(first_sequence_executions_amount <= 100);
  // for (int i = 0; i < first_sequence_executions_amount; i++) {
  //   AddInstructionToCodePage(codepage_no, first_sequence);
  // }
  // AddSerializeInstructionToCodePage(codepage_no);
  //
  // // second sequence
  // AddInstructionToCodePage(codepage_no, second_sequence);
  // AddSerializeInstructionToCodePage(codepage_no);
  //
  // // time measurement sequence
  // AddTimerStartToCodePage(codepage_no);
  // AddInstructionToCodePage(codepage_no, measurement_sequence);
  // AddTimerEndToCodePage(codepage_no);

  // return timing result and epilog
  //MakeTimerResultReturnValue(codepage_no); arm不用我已经把时间差放在X0寄存器中了
  //AddEpilog(codepage_no);

  // make sure that we do not exceed page boundaries
  assert(code_pages_last_written_index_[codepage_no] < kPagesize);
}

void Executor::CreateSpeculativeTriggerTestrunCode(int codepage_no,
                                                   const byte_array& measurement_sequence,
                                                   const byte_array& trigger_sequence,
                                                   const byte_array& reset_sequence,
                                                   int reset_executions_amount) {
  // call rel32
  constexpr char INST_RELATIVE_CALL[] = "\xe8\xff\xff\xff\xff";
  // jmp rel32
  constexpr char INST_RELATIVE_JMP[] = "\xe9\xff\xff\xff\xff";
  // lea rax, [rip + offset]
  constexpr char INST_LEA_RAX_DEREF_RIP_PLUS_OFFSET[] = "\x48\x8d\x05\xff\xff\xff\xff";
  // mov [rsp], rax
  constexpr char INST_MOV_DEREF_RSP_RAX[] = "\x48\x89\x04\x24";
  // ret
  constexpr char INST_RET[] = "\xc3";

  ClearDataPage();
  InitializeCodePage(codepage_no);

  // prolog
  AddProlog(codepage_no);
  AddSerializeInstructionToCodePage(codepage_no);

  // reset microarchitectural state sequence
  // if the number is higher we need to make sure that we have enough "unimportet guardian" stack space
  assert(reset_executions_amount <= 100);
  for (int i = 0; i < reset_executions_amount; i++) {
    AddInstructionToCodePage(codepage_no, reset_sequence);
  }
  AddSerializeInstructionToCodePage(codepage_no);


  //
  // use spectre-RSB to speculatively execute the trigger
  //

  // note that for all following calculations sizeof has the additional '\0', hence the - 1
  // we use this to generate a call which can be misprecided; target is behind the speculated code
  int32_t call_displacement = trigger_sequence.size() + sizeof(INST_RELATIVE_JMP) - 1;
  // we use this to redirect speculation to the same end as the manipulated stack
  int32_t jmp_displacement = sizeof(INST_LEA_RAX_DEREF_RIP_PLUS_OFFSET) - 1 +
      sizeof(INST_MOV_DEREF_RSP_RAX) - 1 + sizeof(INST_RET) - 1;
  // we use this to generate the actual address where we return and replace
  // the saved rip on the stack before calling RET
  int32_t lea_rip_displacement = sizeof(INST_MOV_DEREF_RSP_RAX) - 1 +
      sizeof(INST_RET) - 1;

  byte_array jmp_displacement_encoded = NumberToBytesLE(jmp_displacement, 4);
  byte_array call_displacement_encoded = NumberToBytesLE(call_displacement, 4);
  byte_array lea_rip_displacement_encoded = NumberToBytesLE(lea_rip_displacement, 4);

  // only place opcode and add offset manually
  AddInstructionToCodePage(codepage_no, INST_RELATIVE_CALL, 1);
  AddInstructionToCodePage(codepage_no, call_displacement_encoded);


  // speculation starts here as return address is mispredicted
  AddInstructionToCodePage(codepage_no, trigger_sequence);
  // this is still only accessible during speculation to redirect speculation to the correct jumpout
  // only place opcode and add offset manually
  AddInstructionToCodePage(codepage_no, INST_RELATIVE_JMP, 1);
  AddInstructionToCodePage(codepage_no, jmp_displacement_encoded);
  //
  // speculation ends here
  //


  // Target of CALL_DISPLACEMENT
  // change the return address on the stack to trigger the missspeculation of the RET
  // only place opcode and add offset manually
  AddInstructionToCodePage(codepage_no, INST_LEA_RAX_DEREF_RIP_PLUS_OFFSET,
                                      3);
  AddInstructionToCodePage(codepage_no, lea_rip_displacement_encoded);
  // wanted return address is now in RAX hence we can manipulate the stack now
  AddInstructionToCodePage(codepage_no, INST_MOV_DEREF_RSP_RAX,
                                      4);
  // return address was manipulated hence RET will return to the correct code but
  // will be mispredicted
  AddInstructionToCodePage(codepage_no, INST_RET, 1);

  // target of LEA_RIP_DISPLACEMENT (manipulated RET) and JMP_DISPLACEMENT
  // serialize after trigger
  //page_idx = AddSerializeInstructionToCodePage(page_idx, codepage_no);

  // time measurement sequence
  AddTimerStartToCodePage(codepage_no);
  AddInstructionToCodePage(codepage_no, measurement_sequence);
  AddTimerEndToCodePage(codepage_no);

  // return timing result and epilog
  MakeTimerResultReturnValue(codepage_no);
  AddEpilog(codepage_no);

  // make sure that we do not exceed page boundaries
  assert(code_pages_last_written_index_[codepage_no] < kPagesize);
}

int Executor::ExecuteTestrun(int codepage_no, uint64_t* cycles_elapsed) {
  return ExecuteCodePage(execution_code_pages_[codepage_no], cycles_elapsed);
}

void Executor::ClearDataPage() {
  for (const auto& datapage : execution_data_pages_) {
    memset(datapage, '\0', kPagesize);
  }
}





  //初始化指定的代码页
  void Executor::InitializeCodePage(int codepage_no) {
  // ARM64 NOP 和 RET 指令的机器码
  constexpr uint32_t INST_RET = 0xD65F03C0; // RET
  constexpr uint32_t INST_NOP = 0xD503201F; // NOP

  assert(codepage_no < static_cast<int>(execution_code_pages_.size()));

  // 填充 NOP 指令
  uint32_t* page = reinterpret_cast<uint32_t*>(execution_code_pages_[codepage_no]);
  size_t num_nops = kPagesize / sizeof(uint32_t);
  for (size_t i = 0; i < num_nops; ++i) {
    page[i] = INST_NOP;
  }

  // 添加 RET 指令作为最后一条指令
  page[num_nops - 1] = INST_RET;

  // 重置写入索引
  code_pages_last_written_index_[codepage_no] = 0;
}




//arm prolog
//   void Executor::AddProlog_AArch64(int codepage_no) {
//     // 保存调用者保存的寄存器
//     constexpr char INST_STP_X19_X20[] = "\xfd\x7b\xbf\xa9"; // STP X19, X20, [SP, #-16]!
//     constexpr char INST_STP_X21_X22[] = "\xfd\x7b\xbf\xa9"; // STP X21, X22, [SP, #-16]!
//     constexpr char INST_STP_X23_X24[] = "\xfd\x7b\xbf\xa9"; // STP X23, X24, [SP, #-16]!
//     constexpr char INST_STP_X25_X26[] = "\xfd\x7b\xbf\xa9"; // STP X25, X26, [SP, #-16]!
//     constexpr char INST_STP_X27_X28[] = "\xfd\x7b\xbf\xa9"; // STP X27, X28, [SP, #-16]!
//     constexpr char INST_STP_FP_LR[] = "\xfd\x7b\xbf\xa9";   // STP FP, LR, [SP, #-16]!
//
//     AddInstructionToCodePage(codepage_no, INST_STP_X19_X20, 4);
//     AddInstructionToCodePage(codepage_no, INST_STP_X21_X22, 4);
//     AddInstructionToCodePage(codepage_no, INST_STP_X23_X24, 4);
//     AddInstructionToCodePage(codepage_no, INST_STP_X25_X26, 4);
//     AddInstructionToCodePage(codepage_no, INST_STP_X27_X28, 4);
//     AddInstructionToCodePage(codepage_no, INST_STP_FP_LR, 4);
//
//     // 保存 SIMD 寄存器
//     constexpr char INST_STP_Q8_Q9[] = "\xfd\x7b\xbf\xa9"; // STP Q8, Q9, [SP, #-32]!
//     constexpr char INST_STP_Q10_Q11[] = "\xfd\x7b\xbf\xa9"; // STP Q10, Q11, [SP, #-32]!
//
//     AddInstructionToCodePage(codepage_no, INST_STP_Q8_Q9, 4);
//     AddInstructionToCodePage(codepage_no, INST_STP_Q10_Q11, 4);
//
//     // 分配栈空间
//     constexpr char INST_SUB_SP[] = "\xff\x43\x00\xd1"; // SUB SP, SP, #4096
//     AddInstructionToCodePage(codepage_no, INST_SUB_SP, 4);
//
//     // 初始化寄存器
//     constexpr char INST_MOV_X0[] = "\xa0\x00\x80\xd2"; // MOV X0, #0x1000000
//     constexpr char INST_MOV_X1[] = "\xa1\x00\x80\xd2"; // MOV X1, #0x1000000
//     constexpr char INST_MOV_X2[] = "\xa2\x00\x80\xd2"; // MOV X2, #0x1000000
//
//     AddInstructionToCodePage(codepage_no, INST_MOV_X0, 4);
//     AddInstructionToCodePage(codepage_no, INST_MOV_X1, 4);
//     AddInstructionToCodePage(codepage_no, INST_MOV_X2, 4);
//
//     // 清理状态
//     constexpr char INST_DSB[] = "\xbf\x3f\x03\xd5"; // DSB SY
//     AddInstructionToCodePage(codepage_no, INST_DSB, 4);
// }

void Executor::AddProlog(int codepage_no) {
    // ARM64 Prolog 指令字节序列

    // 保存被调用者保存的寄存器 x19-x28
    constexpr char INST_STP_X19_X20[] = "\x93\x7B\xBF\xA9"; // stp x19, x20, [sp, #-16]!
    constexpr char INST_STP_X21_X22[] = "\x95\x7B\xBF\xA9"; // stp x21, x22, [sp, #-16]!
    constexpr char INST_STP_X23_X24[] = "\x97\x7B\xBF\xA9"; // stp x23, x24, [sp, #-16]!
    constexpr char INST_STP_X25_X26[] = "\x99\x7B\xBF\xA9"; // stp x25, x26, [sp, #-16]!
    constexpr char INST_STP_X27_X28[] = "\x9B\x7B\xBF\xA9"; // stp x27, x28, [sp, #-16]!

    // 保存帧指针 (x29) 和链接寄存器 (x30)
    constexpr char INST_STP_X29_X30[] = "\x9D\x7B\xBF\xA9"; // stp x29, x30, [sp, #-16]!

    // 设置帧指针
    constexpr char INST_MOV_X29_SP[] = "\xFD\x03\x00\x91"; // mov x29, sp

    // 调整栈空间
    constexpr char INST_SUB_SP_0x1000[] = "\x03\xF0\x80\xD2"; // sub sp, sp, #0x1000

    // 初始化寄存器 x8 为 0xFFFFFFFFFFFFFFFF
    constexpr char INST_MOVZ_X8[] = "\x00\x00\x40\xD2";        // movz x8, #0xFFFF, lsl #0
    constexpr char INST_MOVK_X8_LSL16[] = "\xFF\xFF\x50\xF2"; // movk x8, #0xFFFF, lsl #16
    constexpr char INST_MOVK_X8_LSL32[] = "\xFF\xFF\x60\xF2"; // movk x8, #0xFFFF, lsl #32
    constexpr char INST_MOVK_X8_LSL48[] = "\xFF\xFF\x70\xF2"; // movk x8, #0xFFFF, lsl #48

    // 初始化浮点寄存器 v0 为 v8
    constexpr char INST_MOV_V0_V8[] = "\xC0\x6E\x49\x66"; // mov v0.16b, v8.16b

    // 添加指令到代码页
    // 保存被调用者保存的寄存器
    AddInstructionToCodePage(codepage_no, INST_STP_X19_X20, sizeof(INST_STP_X19_X20) - 1);
    AddInstructionToCodePage(codepage_no, INST_STP_X21_X22, sizeof(INST_STP_X21_X22) - 1);
    AddInstructionToCodePage(codepage_no, INST_STP_X23_X24, sizeof(INST_STP_X23_X24) - 1);
    AddInstructionToCodePage(codepage_no, INST_STP_X25_X26, sizeof(INST_STP_X25_X26) - 1);
    AddInstructionToCodePage(codepage_no, INST_STP_X27_X28, sizeof(INST_STP_X27_X28) - 1);

    // 保存帧指针和链接寄存器
    AddInstructionToCodePage(codepage_no, INST_STP_X29_X30, sizeof(INST_STP_X29_X30) - 1);

    // 设置帧指针
    AddInstructionToCodePage(codepage_no, INST_MOV_X29_SP, sizeof(INST_MOV_X29_SP) - 1);

    // 调整栈空间
    AddInstructionToCodePage(codepage_no, INST_SUB_SP_0x1000, sizeof(INST_SUB_SP_0x1000) - 1);

    // 初始化寄存器 x8 为 0xFFFFFFFFFFFFFFFF
    AddInstructionToCodePage(codepage_no, INST_MOVZ_X8, sizeof(INST_MOVZ_X8) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X8_LSL16, sizeof(INST_MOVK_X8_LSL16) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X8_LSL32, sizeof(INST_MOVK_X8_LSL32) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X8_LSL48, sizeof(INST_MOVK_X8_LSL48) - 1);

    // 初始化浮点寄存器 v0 为 v8
    AddInstructionToCodePage(codepage_no, INST_MOV_V0_V8, sizeof(INST_MOV_V0_V8) - 1);

    // 继续初始化其他寄存器（如 x0, x1, x2, x3 等）按照相同的方式
    // 例如，初始化 x0 为 0xFFFFFFFFFFFFFFFF
    constexpr char INST_MOVZ_X0[] = "\x00\x00\x00\xD2";        // movz x0, #0xFFFF, lsl #0
    constexpr char INST_MOVK_X0_LSL16[] = "\xFF\xFF\x10\xF2"; // movk x0, #0xFFFF, lsl #16
    constexpr char INST_MOVK_X0_LSL32[] = "\xFF\xFF\x20\xF2"; // movk x0, #0xFFFF, lsl #32
    constexpr char INST_MOVK_X0_LSL48[] = "\xFF\xFF\x30\xF2"; // movk x0, #0xFFFF, lsl #48

    AddInstructionToCodePage(codepage_no, INST_MOVZ_X0, sizeof(INST_MOVZ_X0) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X0_LSL16, sizeof(INST_MOVK_X0_LSL16) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X0_LSL32, sizeof(INST_MOVK_X0_LSL32) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X0_LSL48, sizeof(INST_MOVK_X0_LSL48) - 1);

    // 按照需要继续初始化 x1, x2, x3 等寄存器

    // 例如，初始化 x1
    constexpr char INST_MOVZ_X1[] = "\x00\x00\x01\xD2";        // movz x1, #0xFFFF, lsl #0
    constexpr char INST_MOVK_X1_LSL16[] = "\xFF\xFF\x11\xF2"; // movk x1, #0xFFFF, lsl #16
    constexpr char INST_MOVK_X1_LSL32[] = "\xFF\xFF\x21\xF2"; // movk x1, #0xFFFF, lsl #32
    constexpr char INST_MOVK_X1_LSL48[] = "\xFF\xFF\x31\xF2"; // movk x1, #0xFFFF, lsl #48

    AddInstructionToCodePage(codepage_no, INST_MOVZ_X1, sizeof(INST_MOVZ_X1) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X1_LSL16, sizeof(INST_MOVK_X1_LSL16) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X1_LSL32, sizeof(INST_MOVK_X1_LSL32) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X1_LSL48, sizeof(INST_MOVK_X1_LSL48) - 1);

    // 初始化浮点寄存器 v0 为 v8
    // 如果需要初始化更多浮点寄存器，可以继续添加相应的指令
}
  // 插入一段序言代码（Prolog），其目的是为后续代码执行创建一个安全且受控的环境。具体包括：
  //
  // 保存必要的寄存器和硬件状态（如栈指针、寄存器值）。
  // 为程序执行分配足够的栈空间。
  // 初始化一些特定寄存器的值。
  // 确保硬件状态符合预期，避免意外错误（如浮点异常）。
void Executor::AddEpilog(int codepage_no) {
    // 恢复初始化的浮点寄存器 v0
    constexpr char INST_MOV_V0_V8[] = "\xC0\x6E\x49\x66"; // mov v0.16b, v8.16b

    // 恢复被初始化的寄存器 x0, x1, x8 等
    // 例如，恢复 x0
    constexpr char INST_MOVK_X0_LSL48[] = "\xFF\xFF\x31\xF2"; // movk x0, #0xFFFF, lsl #48
    constexpr char INST_MOVK_X0_LSL32[] = "\xFF\xFF\x21\xF2"; // movk x0, #0xFFFF, lsl #32
    constexpr char INST_MOVK_X0_LSL16[] = "\xFF\xFF\x11\xF2"; // movk x0, #0xFFFF, lsl #16
    constexpr char INST_MOVZ_X0[] = "\x00\x00\x00\xD2";        // movz x0, #0xFFFF, lsl #0

    // 恢复寄存器 x8
    constexpr char INST_MOVK_X8_LSL48[] = "\xFF\xFF\x70\xF2"; // movk x8, #0xFFFF, lsl #48
    constexpr char INST_MOVK_X8_LSL32[] = "\xFF\xFF\x60\xF2"; // movk x8, #0xFFFF, lsl #32
    constexpr char INST_MOVK_X8_LSL16[] = "\xFF\xFF\x50\xF2"; // movk x8, #0xFFFF, lsl #16
    constexpr char INST_MOVZ_X8[] = "\x00\x00\x40\xD2";        // movz x8, #0xFFFF, lsl #0

    // 恢复栈空间
    constexpr char INST_ADD_SP_0x1000[] = "\x03\xF0\x80\x52"; // add sp, sp, #0x1000

    // 恢复帧指针
    constexpr char INST_MOV_SP_X29[] = "\xFD\x03\x00\x91"; // mov sp, x29

    // 恢复帧指针和链接寄存器
    constexpr char INST_LDP_X29_X30[] = "\x9D\x7B\xBF\xA9"; // ldp x29, x30, [sp], #16

    // 恢复被调用者保存的寄存器 x27-x19
    constexpr char INST_LDP_X27_X28[] = "\x9B\x7B\xBF\xA9"; // ldp x27, x28, [sp], #16
    constexpr char INST_LDP_X25_X26[] = "\x99\x7B\xBF\xA9"; // ldp x25, x26, [sp], #16
    constexpr char INST_LDP_X23_X24[] = "\x97\x7B\xBF\xA9"; // ldp x23, x24, [sp], #16
    constexpr char INST_LDP_X21_X22[] = "\x95\x7B\xBF\xA9"; // ldp x21, x22, [sp], #16
    constexpr char INST_LDP_X19_X20[] = "\x93\x7B\xBF\xA9"; // ldp x19, x20, [sp], #16

    // 恢复浮点寄存器 v0
    AddInstructionToCodePage(codepage_no, INST_MOV_V0_V8, sizeof(INST_MOV_V0_V8) - 1);

    // 恢复寄存器 x0
    AddInstructionToCodePage(codepage_no, INST_MOVK_X0_LSL48, sizeof(INST_MOVK_X0_LSL48) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X0_LSL32, sizeof(INST_MOVK_X0_LSL32) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X0_LSL16, sizeof(INST_MOVK_X0_LSL16) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVZ_X0, sizeof(INST_MOVZ_X0) - 1);

    // 恢复寄存器 x8
    AddInstructionToCodePage(codepage_no, INST_MOVK_X8_LSL48, sizeof(INST_MOVK_X8_LSL48) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X8_LSL32, sizeof(INST_MOVK_X8_LSL32) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVK_X8_LSL16, sizeof(INST_MOVK_X8_LSL16) - 1);
    AddInstructionToCodePage(codepage_no, INST_MOVZ_X8, sizeof(INST_MOVZ_X8) - 1);

    // 调整栈指针回原始位置
    AddInstructionToCodePage(codepage_no, INST_ADD_SP_0x1000, sizeof(INST_ADD_SP_0x1000) - 1);

    // 恢复帧指针和链接寄存器
    AddInstructionToCodePage(codepage_no, INST_MOV_SP_X29, sizeof(INST_MOV_SP_X29) - 1);
    AddInstructionToCodePage(codepage_no, INST_LDP_X29_X30, sizeof(INST_LDP_X29_X30) - 1);

    // 恢复被调用者保存的寄存器
    AddInstructionToCodePage(codepage_no, INST_LDP_X27_X28, sizeof(INST_LDP_X27_X28) - 1);
    AddInstructionToCodePage(codepage_no, INST_LDP_X25_X26, sizeof(INST_LDP_X25_X26) - 1);
    AddInstructionToCodePage(codepage_no, INST_LDP_X23_X24, sizeof(INST_LDP_X23_X24) - 1);
    AddInstructionToCodePage(codepage_no, INST_LDP_X21_X22, sizeof(INST_LDP_X21_X22) - 1);
    AddInstructionToCodePage(codepage_no, INST_LDP_X19_X20, sizeof(INST_LDP_X19_X20) - 1);

    // 恢复浮点寄存器 v0
    AddInstructionToCodePage(codepage_no, INST_MOV_V0_V8, sizeof(INST_MOV_V0_V8) - 1);

    // 最后，返回到调用者
    // ret
    constexpr char INST_RET[] = "\xC0\x03\x5F\xD6"; // ret
    AddInstructionToCodePage(codepage_no, INST_RET, sizeof(INST_RET) - 1);
}
void Executor::AddSerializeInstructionToCodePage(int codepage_no) {
  // insert CPUID to serialize instruction stream
  constexpr char INST_DSB[] = "\x9F\x30\x03\xD5";  // dsb sy
  constexpr char INST_ISB[] = "\x1F\x20\x03\xD5";  // isb sy

  AddInstructionToCodePage(codepage_no, INST_DSB, 4);
  AddInstructionToCodePage(codepage_no, INST_ISB, 4);

}

//code for arm,maybe need to recode
  void Executor::AddTimerStartToCodePage(int codepage_no) {
  // 定义 ARM 指令的机器码（AArch64，小端序）
  constexpr char INST_DMB_SY[] = "\x7f\x23\x03\xd5";              // DMB SY
  constexpr char INST_MOV_X0_0[] = "\x00\x00\x80\xd2";          // MOV X9, #0
  constexpr char INST_ISB_SY[] = "\x1F\x20\x03\xd5";             // ISB SY
  constexpr char INST_MRS_X0_PMCCNTR_EL0[] = "\x1f\x20\x03\xd5"; // MRS X9, PMCCNTR_EL0
  constexpr char INST_MOV_X10_X0[] = "\xAA\x1F\x03\xD6";          // MOV X10, X9

  // 添加 ARM 指令到代码页，每条指令长度为 4 字节
  AddInstructionToCodePage(codepage_no, INST_DMB_SY, 4);
  AddInstructionToCodePage(codepage_no, INST_MOV_X0_0, 4);
  AddInstructionToCodePage(codepage_no, INST_ISB_SY, 4);
  AddInstructionToCodePage(codepage_no, INST_MRS_X0_PMCCNTR_EL0, 4);
  AddInstructionToCodePage(codepage_no, INST_MOV_X10_X0, 4);
}


//   设置计时起点：
// 利用硬件支持的时间戳计数器（如 x86 架构中的 RDTSC 指令）记录当前时间戳。
// 将读取的时间戳值存储到寄存器 R10 中，便于后续与结束时间戳计算差值。
// 序列化指令流：
// 确保在计时前的所有指令都已完成，避免由于乱序执行导致的时间测量不准确。
// 使用 MFENCE 和 CPUID 来实现指令流的同步。
// void Executor::AddTimerStartToCodePage(int codepage_no) {
//   constexpr char INST_MFENCE[] = "\x0f\xae\xf0";
//   constexpr char INST_XOR_EAX_EAX_CPUID[] = "\x31\xc0\x0f\xa2";
//   // note that we can use R10 as it is caller-saved
//   constexpr char INST_MOV_R10_RAX[] = "\x49\x89\xc2";
//
//   AddInstructionToCodePage(codepage_no, INST_MFENCE, 3);
//   AddInstructionToCodePage(codepage_no, INST_XOR_EAX_EAX_CPUID, 4);
// #if defined(INTEL)
//   constexpr char INST_RDTSC[] = "\x0f\x31";
//   AddInstructionToCodePage(codepage_no, INST_RDTSC, 2);
// #elif defined(AMD)
//   constexpr char INST_MOV_ECX_1_RDPRU[] = "\xb9\x01\x00\x00\x00\x0f\x01\xfd";
//   // for AMD we use RDPRU to read the APERF register which makes a more stable timer than RDTSC
//   AddInstructionToCodePage(codepage_no, INST_MOV_ECX_1_RDPRU, 8);
// #endif
//   // move result to R10 s.t. we can use it later in AddTimerEndToCodePage
//   AddInstructionToCodePage(codepage_no, INST_MOV_R10_RAX,3);
// }


  //code for arm,maybe need to recode
  void Executor::AddTimerEndToCodePage(int codepage_no) {
  // 定义 ARM 指令的机器码（AArch64，小端序）
  constexpr char INST_MOV_X0_0[] = "\x00\x00\x80\xd2";             // MOV X9, #0
  constexpr char INST_ISB_SY[] = "\x1F\x20\x03\xd5";               // ISB SY
  constexpr char INST_MRS_X0_PMCCNTR_EL0[] = "\x39\x00\x00\xd5";  // MRS X9, PMCCNTR_EL0
  constexpr char INST_SUB_X11_X0_X10[] = "\x4f\x3f\x1f\xd3";  // SUB X11, X9, X10
  constexpr char INST_MOV_X11_X0[] = "\x1F\x20\x03\xd5";       // MOV X0, X11

  // 添加 ARM 指令到代码页，每条指令长度为 4 字节
  AddInstructionToCodePage(codepage_no, INST_MOV_X0_0, 4);                // MOV X0, #0
  AddInstructionToCodePage(codepage_no, INST_ISB_SY, 4);                  // ISB SY
  AddInstructionToCodePage(codepage_no, INST_MRS_X0_PMCCNTR_EL0, 4);      // MRS X0, PMCCNTR_EL0
  AddInstructionToCodePage(codepage_no, INST_SUB_X11_X0_X10, 4);          // SUB X11, X0, X10
  AddInstructionToCodePage(codepage_no, INST_MOV_X11_X0, 4);              // MOV X11, X0
}


//   读取结束时间戳：
// 利用处理器时间戳（如 RDTSC 或 RDTSCP）记录结束时间。
// 计算时间差：
// 结束时间与起始时间（存储在 R10 寄存器中）做减法，得到执行时间差。
// 时间差存储在寄存器 R11 中，以便后续使用。
// 序列化指令流：
// 确保读取时间戳的操作是准确的，不受指令乱序或处理器流水线的影响。
// void Executor::AddTimerEndToCodePage(int codepage_no) {
//   constexpr char INST_XOR_EAX_EAX_CPUID[] = "\x31\xc0\x0f\xa2";
//   constexpr char INST_SUB_RAX_R10[] = "\x4c\x29\xd0";
//   // note that we can use R11 as it is caller-saved
//   constexpr char INST_MOV_R11_RAX[] = "\x49\x89\xc3";
//
// #if defined(INTEL)
//   constexpr char INST_RDTSCP[] = "\x0f\x01\xf9";
//   AddInstructionToCodePage(codepage_no, INST_RDTSCP, 3);
// #elif defined(AMD)
//   // for AMD we use RDPRU to read the APERF register which makes a more stable timer than RDTSC
//   constexpr char INST_MFENCE[] = "\x0f\xae\xf0";
//   constexpr char INST_MOV_ECX_1_RDPRU[] = "\xb9\x01\x00\x00\x00\x0f\x01\xfd";
//   AddInstructionToCodePage(codepage_no, INST_MFENCE, 3);
//   AddInstructionToCodePage(codepage_no, INST_XOR_EAX_EAX_CPUID, 4);
//   AddInstructionToCodePage(codepage_no, INST_MOV_ECX_1_RDPRU, 8);
// #endif
//   AddInstructionToCodePage(codepage_no, INST_SUB_RAX_R10, 3);
//   AddInstructionToCodePage(codepage_no, INST_MOV_R11_RAX, 3);
//   AddInstructionToCodePage(codepage_no, INST_XOR_EAX_EAX_CPUID, 4);
// }

  //把指定的机器指令字节写入到代码页

  //将指令写入代码页的作用主要在于支持 动态代码生成 和 运行时执行，通过这种方式，程序可以根据运行时环境动态生成、调整并执行代码，从而实现更高的性能和灵活性。
void Executor::AddInstructionToCodePage(int codepage_no,
                                          const char* instruction_bytes,
                                          size_t instruction_length) {
  size_t page_idx = code_pages_last_written_index_[codepage_no];
  if (page_idx + instruction_length >= kPagesize) {
    std::stringstream sstream;
    sstream << "Problematic code page is at address 0x"
            << std::hex << reinterpret_cast<int64_t>(execution_code_pages_[codepage_no]);
    LOG_DEBUG(sstream.str());
    LOG_ERROR("Generated code exceeds page boundary");
    std::abort();
  }

  assert(codepage_no < static_cast<int>(execution_code_pages_.size()));
  memcpy(execution_code_pages_[codepage_no] + page_idx, instruction_bytes, instruction_length);
  code_pages_last_written_index_[codepage_no] = page_idx + instruction_length;
}

  //overload
void Executor::AddInstructionToCodePage(int codepage_no,
                                          const byte_array& instruction_bytes) {
  size_t page_idx = code_pages_last_written_index_[codepage_no];
  if (page_idx + instruction_bytes.size() >= kPagesize) {
    std::stringstream sstream;
    sstream << "Problematic code page is at address 0x"
      << std::hex << reinterpret_cast<int64_t>(execution_code_pages_[codepage_no]);
    LOG_DEBUG(sstream.str());
    LOG_ERROR("Generated code exceeds page boundary (" +
      std::to_string(page_idx + instruction_bytes.size()) + "/" + std::to_string(kPagesize) + ")");
    std::abort();
  }

  size_t new_page_idx = page_idx;
  assert(codepage_no < static_cast<int>(execution_code_pages_.size()));
  for (std::byte b : instruction_bytes) {
    execution_code_pages_[codepage_no][new_page_idx] = std::to_integer<int>(b);
    new_page_idx++;
  }
  code_pages_last_written_index_[codepage_no] = new_page_idx;
}


// code for arm,maybe need to recode
//   void Executor::MakeTimerResultReturnValue(int codepage_no) {
//   constexpr char MOV_X0_X11[] = "\xaa\x0b\x00\xe0"; // MOV X0, X11
//
//   AddInstructionToCodePage(codepage_no, MOV_X0_X11, 4);
// }


  //这段代码的功能是将定时器的结果存储到 RAX 寄存器中，以便返回给调用者或用于后续处理。
void Executor::MakeTimerResultReturnValue(int codepage_no) {
  //remake for arm
  //constexpr char MOV_RAX_R11[] = "\x4c\x89\xd8";
  constexpr char MOV_RAX_R11[] = "\xaa\x0b\x00\xe0"; // MOV X0, X11

  assert(code_pages_last_written_index_[codepage_no] + 3 < kPagesize);

  AddInstructionToCodePage(codepage_no, MOV_RAX_R11, 3);
}


  //生成x86的nops指令，对arm需要recode
// 在 ARM 中，NOP 的机器码是固定的，具体值依架构版本而定：
// ARM 模式（32 位指令集）：
// 指令编码为：0xE1A00000。
// 表示：MOV R0, R0（将寄存器 R0 的值移动到自身，不执行任何实际操作）。
// Thumb 模式（16 位指令集）：
// 指令编码为：0xBF00。
// 表示：无操作。
// AArch64（64 位 ARM）模式：
// 指令编码为：0xD503201F。
// 表示：无操作。
// byte_array Executor::CreateSequenceOfNOPs(size_t length) {
//   //constexpr auto INST_NOP_AS_DECIMAL = static_cast<unsigned char>(0x90);
//   constexpr auto INST_NOP_AS_DECIMAL = static_cast<unsigned int>(0xD503201F); // ARM NOP
//   byte_array nops;
//   std::byte nop_byte{INST_NOP_AS_DECIMAL};
//   for (size_t i = 0; i < length; i++) {
//     nops.push_back(nop_byte);
//   }
//   return nops;
// }
  byte_array Executor::CreateSequenceOfNOPs(size_t length) {
  //constexpr uint32_t ARM64_NOP = 0xD503201F;
  byte_array nops;
  constexpr auto ARM64_NOP_0 = static_cast<unsigned char>(0xD5);
  constexpr auto ARM64_NOP_1 = static_cast<unsigned char>(0x03);
  constexpr auto ARM64_NOP_2 = static_cast<unsigned char>(0x20);
  constexpr auto ARM64_NOP_3 = static_cast<unsigned char>(0x1F);
  std::byte nop_byte0{ARM64_NOP_0};
  std::byte nop_byte1{ARM64_NOP_1};
  std::byte nop_byte2{ARM64_NOP_2};
  std::byte nop_byte3{ARM64_NOP_3};
  for (size_t i = 0; i < length; i+=4) {
    nops.push_back(nop_byte0);
    nops.push_back(nop_byte1);
    nops.push_back(nop_byte2);
    nops.push_back(nop_byte3);
  }

  return nops;
}

//
// fault handling logic
//
static jmp_buf fault_handler_jump_buf;

// Fault counters
static int sigsegv_no = 0;
static int sigfpe_no = 0;
static int sigill_no = 0;
static int sigtrap_no = 0;

void Executor::PrintFaultCount() {
  std::cout << "=== Faultcounters of Executor ===" << std::endl
            << "\tSIGSEGV: " << sigsegv_no << std::endl
            << "\tSIGFPE: " << sigfpe_no << std::endl
            << "\tSIGILL: " << sigill_no << std::endl
            << "\tSIGTRAP: " << sigtrap_no << std::endl
            << "=================================" << std::endl;
}
  void Executor::FaultHandler(int sig) {
  // NOTE: this function and Executor::ExecuteCodePage must both be static functions
  //       for the signal handling + jmp logic to work
  switch (sig) {
  case SIGSEGV:sigsegv_no++;
    break;
  case SIGFPE:sigfpe_no++;
    break;
  case SIGILL:sigill_no++;
    break;
  case SIGTRAP:sigtrap_no++;
    break;
  default:std::abort();
  }

  // jump back to the previously stored fallback point
  longjmp(fault_handler_jump_buf, 1);
}

template<size_t size>
void Executor::RegisterFaultHandler(std::array<int, size> signals_to_handle) {
  for (int sig : signals_to_handle) {
    signal(sig, Executor::FaultHandler);
  }
}
 void ScanCodePage(void* codepage, size_t size) {
  uint8_t* byte_ptr = (uint8_t*)codepage;
  for (size_t i = 0; i < size; i+=4) {
    printf(" 0x%02x%02x%02x%02x \n", byte_ptr[i],byte_ptr[i+1],byte_ptr[i+2],byte_ptr[i+3]);
  }
}
template<size_t size>
void Executor::UnregisterFaultHandler(std::array<int, size> signals_to_handle) {
  for (int sig : signals_to_handle) {
    signal(sig, SIG_DFL);
  }
}

  //是一个动态代码执行的框架，能够安全地运行生成的代码页，同时测量其执行时间，并捕获运行时异常 need to change
  //arm需要修改
__attribute__((no_sanitize("address")))
  int Executor::ExecuteCodePage(void* codepage, uint64_t* cycles_elapsed) {
  /// NOTE: this function and Executor::FaultHandler must both be static functions
///       for the signal handling + jmp logic to work


#if DEBUGMODE == 1
  // list of signals that we catch and throw as errors
  // (without DEBUGMODE the array is defined in the error case)
  std::array<int, 4> signals_to_handle = {SIGSEGV, SIGILL, SIGFPE, SIGTRAP};
  // register fault handler (if not in debugmode we do this in constructor/destructor as
  //    this has a huge impact on the runtime)
  RegisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
#endif

  if (!setjmp(fault_handler_jump_buf)) {
    // jump to codepage
    uint64_t cycle_diff = ((uint64_t(*)()) codepage)();
    // set return argument
    *cycles_elapsed = cycle_diff;

#if DEBUGMODE == 1
    // unregister signal handler (if not in debugmode we do this in constructor/destructor as
    // this has a huge impact on the runtime)
    UnregisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
#endif

    return 0;
  } else {
    ScanCodePage(codepage, kPagesize);
    // if we reach this; the code has caused a fault

    // unmask the signal again as we reached this point directly from the signal handler
#if DEBUGMODE == 0
    // only allocate the array in case of an error to safe execution time
    // list of signals that we catch and throw as errors
    std::array<int, 4> signals_to_handle = {SIGSEGV, SIGILL, SIGFPE, SIGTRAP};
#endif

    sigset_t signal_set;
    sigemptyset(&signal_set);
    for (int sig : signals_to_handle) {
      sigaddset(&signal_set, sig);
    }
    sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);

#if DEBUGMODE == 1
    // unregister signal handler (if not in debugmode we do this in constructor/destructor as
    // this has a huge impact on the runtime)
    UnregisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
#endif

    // report that we crashed
    *cycles_elapsed = -1;
    return 1;
  }
}

}  // namespace osiris
