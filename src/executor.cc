// // Copyright 2021 Daniel Weber, Ahmad Ibrahim, Hamed Nemati, Michael Schwarz, Christian Rossow
// //
// // Licensed under the Apache License, Version 2.0 (the "License");
// // you may not use this file except in compliance with the License.
// // You may obtain a copy of the License at
// //
// //     http://www.apache.org/licenses/LICENSE-2.0
// //
// // Unless required by applicable law or agreed to in writing, software
// // distributed under the License is distributed on an "AS IS" BASIS,
// // WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// // See the License for the specific language governing permissions and
// //     limitations under the License.
//
//
// #include "executor.h"
//
// #include <setjmp.h>
// #include <signal.h>
// #include <sys/mman.h>
//
// #include <cassert>
// #include <cstddef>
// #include <cstring>
// #include <iostream>
// #include <sstream>
//
// #include "code_generator.h"
// #include "logger.h"
// #include <arm_neon.h>   // 一些 ARM 架构特定的内置函数和数据类型
//
// namespace osiris {
//
// Executor::Executor() {
//   // allocate memory for memory accesses during execution
//   for (size_t i = 0; i < execution_data_pages_.size(); i++) {
//     void* addr = reinterpret_cast<void*>(kMemoryBegin + i * kPagesize);
//     // check that page is not mapped
//     int ret = msync(addr, kPagesize, 0);
//     if (ret != -1 || errno != ENOMEM) {
//       LOG_ERROR("Execution page is already mapped. Aborting!");
//       std::exit(1);
//     }
//     char* page = static_cast<char*>(mmap(addr,
//                                          kPagesize,
//                                          PROT_READ | PROT_WRITE,
//                                          MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
//                                          -1,
//                                          0));
//
//     if (page != reinterpret_cast<void*>(kMemoryBegin + i* kPagesize) || page == MAP_FAILED) {
//       LOG_ERROR("Couldn't allocate memory for execution (data memory). Aborting!");
//       std::exit(1);
//     }
//     execution_data_pages_[i] = page;
//   }
//
//   // allocate memory that holds the actual instructions we execute
//   for (size_t i = 0; i < execution_code_pages_.size(); i++) {
//     execution_code_pages_[i] = static_cast<char*>(mmap(nullptr,
//                                                        kPagesize,
//                                                        PROT_READ | PROT_WRITE | PROT_EXEC,
//                                                        MAP_PRIVATE | MAP_ANONYMOUS,
//                                                        -1,
//                                                        0));
//     if (execution_code_pages_[i] == MAP_FAILED) {
//       LOG_ERROR("Couldn't allocate memory for execution (exec memory). Aborting!");
//       std::exit(1);
//     }
//   }
//
// #if DEBUGMODE == 0
//   // if we are not in DEBUGMODE this will instead be inlined in Executor::ExecuteCodePage()
//   std::cout << "DEBUGMODE ==0";
//   std::array<int, 4> signals_to_handle = {SIGSEGV, SIGILL, SIGFPE, SIGTRAP};
//   // register fault handler
//   RegisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
// #endif
// }
//
// Executor::~Executor()
// {
// #if DEBUGMODE == 0
//   // if we are not in DEBUGMODE this will instead be inlined in Executor::ExecuteCodePage()
//   std::array<int, 4> signals_to_handle = {SIGSEGV, SIGILL, SIGFPE, SIGTRAP};
//   UnregisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
// #endif
// }
//
// int Executor::TestResetSequence(const byte_array& trigger_sequence,
//                                 const byte_array& measurement_sequence,
//                                 const byte_array& reset_sequence,
//                                 int no_testruns,
//                                 int reset_executions_amount,
//                                 int64_t* cycles_difference) {
//   byte_array nop_sequence = CreateSequenceOfNOPs(reset_sequence.size());//生成nop序列
//   std::vector<int64_t> clean_runs;
//   std::vector<int64_t> noisy_runs;
//   clean_runs.reserve(no_testruns);//预留no_testruns个空间
//   noisy_runs.reserve(no_testruns);
//
//   // intuition:
//   //  given a valid measure and trigger sequence:
//   //  when reset;measure == trigger;reset;measure (or very small diff) -> reset sequence works
//   CreateResetTestrunCode(0, nop_sequence, measurement_sequence, reset_sequence,
//                          reset_executions_amount);
//   CreateResetTestrunCode(1, trigger_sequence, measurement_sequence, reset_sequence,
//                          reset_executions_amount);
//   for (int i = 0; i < no_testruns; i++) {
//     // get timing with reset sequence
//     uint64_t cycles_elapsed_reset_measure;
//     int error = ExecuteTestrun(0, &cycles_elapsed_reset_measure);
//     if (error) {
//       // abort
//       *cycles_difference = -1;
//       return 1;
//     }
//     clean_runs.push_back(cycles_elapsed_reset_measure);
//   }
//
//   for (int i = 0; i < no_testruns; i++) {
//     // get timing without reset sequence
//     uint64_t cycles_elapsed_trigger_reset_measure;
//     int error = ExecuteTestrun(1, &cycles_elapsed_trigger_reset_measure);
//     if (error) {
//       // abort
//       *cycles_difference = -1;
//       return 1;
//     }
//     noisy_runs.push_back(cycles_elapsed_trigger_reset_measure);
//   }
//   *cycles_difference = static_cast<int64_t>(median<int64_t>(clean_runs) -
//       median<int64_t>(noisy_runs));
//   return 0;
// }
//
// int Executor::TestSequenceTriple(const byte_array& trigger_sequence,
//                                  const byte_array& measurement_sequence,
//                                  const byte_array& reset_sequence,
//                                  int no_testruns,
//                                  int64_t* cycles_difference) {
//   std::vector<int64_t> results;
//   CreateTestrunCode(0, trigger_sequence, reset_sequence, measurement_sequence, 1);
//   CreateTestrunCode(1, reset_sequence, trigger_sequence, measurement_sequence, 1);
//   for (int i = 0; i < no_testruns; i++) {
//     // get timing for first experiment
//     uint64_t cycles_elapsed_trigger_reset;
//     int error = ExecuteTestrun(0, &cycles_elapsed_trigger_reset);
//     if (error) {
//       // abort
//       *cycles_difference = -1;
//       return 1;
//     }
//
//     // get timing for second experiment
//     uint64_t cycles_elapsed_reset_trigger;
//     error = ExecuteTestrun(1, &cycles_elapsed_reset_trigger);
//     if (error) {
//       // abort
//       *cycles_difference = -1;
//       return 1;
//     }
//     int64_t cycles_difference_per_run = static_cast<int64_t>(cycles_elapsed_trigger_reset) -
//         static_cast<int64_t>(cycles_elapsed_reset_trigger);
//     results.push_back(cycles_difference_per_run);
//   }
//   *cycles_difference = static_cast<int64_t>(median<int64_t>(results));
//   return 0;
// }
//   //cleanup 进入这个函数
// //成功返回0
// int Executor::TestTriggerSequence(const byte_array& trigger_sequence,
//                                   const byte_array& measurement_sequence,
//                                   const byte_array& reset_sequence,
//                                   bool execute_trigger_only_in_speculation,
//                                   int no_testruns,//测试次数
//                                   int reset_executions_amount,
//                                   int64_t* cycles_difference) {
//   // disabled for performance reasons (on 2020-09-03 by Osiris dev)
//   // can be enabled again without losing too much performance
//   byte_array nop_sequence; // = CreateSequenceOfNOPs(trigger_sequence.size());
//
//   // vectors are preallocated and just get cleared on everyrun for performance
//   results_trigger.clear();
//   results_notrigger.clear();
//   results_trigger.reserve(no_testruns);
//   results_notrigger.reserve(no_testruns);
//
//
//   if (execute_trigger_only_in_speculation) {
//     CreateSpeculativeTriggerTestrunCode(0, measurement_sequence,
//                                         trigger_sequence,
//                                         reset_sequence, reset_executions_amount);
//     CreateSpeculativeTriggerTestrunCode(1, measurement_sequence,
//                                         nop_sequence,
//                                         reset_sequence, reset_executions_amount);
//   } else {
//
//     CreateTestrunCode(0, reset_sequence, trigger_sequence, measurement_sequence,
//                       reset_executions_amount);
//
//      CreateTestrunCode(1, reset_sequence, nop_sequence, measurement_sequence,
//                        reset_executions_amount);
//
//
//   }
//
//   // get timing with trigger sequence
//   for (int i = 0; i < no_testruns; i++) {
//
//     uint64_t cycles_elapsed_trigger ;
//     //__sync_synchronize();
//     //std::cout<<&cycles_elapsed_trigger<<std::endl;
//     int error = ExecuteTestrun(0, &cycles_elapsed_trigger);
//     if (error) {
//       // abort
//       *cycles_difference = -1;
//       return 1;
//     }
//     //std::cout<<"out cycles_elapsed"<<cycles_elapsed_trigger<<std::endl;
//     if (cycles_elapsed_trigger <= 5000) {
//
//       std::cout<<"cycles_elapsed_trigger"<<cycles_elapsed_trigger<<std::endl;
//       results_trigger.emplace_back(cycles_elapsed_trigger);
//     }
//
//   }
//
// //0x0000515000000580
//
//   //
//   for (int i = 0; i < no_testruns; i++) {
//     // get timing without trigger sequence
//     uint64_t cycles_elapsed_notrigger ;
//
//     int error = ExecuteTestrun(1, &cycles_elapsed_notrigger);
//     if (error) {
//       // abort
//       *cycles_difference = -1;
//       return 1;
//     }
//     if (cycles_elapsed_notrigger <= 5000) {
//       results_notrigger.emplace_back(cycles_elapsed_notrigger);
//     }
//
//   }
//   double median_trigger = median<int64_t>(results_trigger);
//   double median_notrigger = median<int64_t>(results_notrigger);
//   *cycles_difference = static_cast<int64_t>(median_notrigger - median_trigger);
//
//   return 0;
// }
//
// void Executor::CreateResetTestrunCode(int codepage_no, const byte_array& trigger_sequence,
//                                       const byte_array& measurement_sequence,
//                                       const byte_array& reset_sequence,
//                                       int reset_executions_amount) {
//   ClearDataPage();
//   InitializeCodePage(codepage_no);
//
//   // prolog
//   //AddProlog(codepage_no);
//   AddInstructionToCodePage(codepage_no, trigger_sequence);
//   AddSerializeInstructionToCodePage(codepage_no);
//
//   // try to reset microarchitectural state again
//   assert(reset_executions_amount <= 100);  // else we need to increase guardian stack space
//   for (int i = 0; i < reset_executions_amount; i++) {
//     AddInstructionToCodePage(codepage_no, reset_sequence);
//   }
//   AddSerializeInstructionToCodePage(codepage_no);
//
//   // time measurement sequence
//   AddTimerStartToCodePage(codepage_no);
//   AddInstructionToCodePage(codepage_no, measurement_sequence);
//   AddTimerEndToCodePage(codepage_no);
//
//   // return timing result and epilog
//   MakeTimerResultReturnValue(codepage_no);
//   AddEpilog(codepage_no);
//
//   // make sure that we do not exceed page boundaries
//   assert(code_pages_last_written_index_[codepage_no] < kPagesize);
// }
//
// void Executor::CreateTestrunCode(int codepage_no, const byte_array& first_sequence,
//                                  const byte_array& second_sequence,
//                                  const byte_array& measurement_sequence,
//                                  int first_sequence_executions_amount) {
//   ClearDataPage();
//   InitializeCodePage(codepage_no);
//
//   // prolog
//   AddProlog(codepage_no);
//   AddSerializeInstructionToCodePage(codepage_no);
//
//   // first sequence
//   // if we need more we also have to increase the guardian stack space
//
//    assert(first_sequence_executions_amount <= 100);
//    for (int i = 0; i < first_sequence_executions_amount; i++) {
//      AddInstructionToCodePage(codepage_no, first_sequence);
//    }
//   AddSerializeInstructionToCodePage(codepage_no);
//
//   // // second sequence
//   AddInstructionToCodePage(codepage_no, second_sequence);
//   AddSerializeInstructionToCodePage(codepage_no);
//   //
//   // // time measurement sequence
//    AddTimerStartToCodePage(codepage_no);
//    AddInstructionToCodePage(codepage_no, measurement_sequence);
//   AddTimerEndToCodePage(codepage_no);
//
//   // return timing result and epilog
//   MakeTimerResultReturnValue(codepage_no);
//   AddEpilog(codepage_no);
//
//   // make sure that we do not exceed page boundaries
//   assert(code_pages_last_written_index_[codepage_no] < kPagesize);
// }
//
// void Executor::CreateSpeculativeTriggerTestrunCode(int codepage_no,
//                                                    const byte_array& measurement_sequence,
//                                                    const byte_array& trigger_sequence,
//                                                    const byte_array& reset_sequence,
//                                                    int reset_executions_amount) {
//   // call rel32
//   constexpr char INST_RELATIVE_CALL[] = "\xe8\xff\xff\xff\xff";
//   // jmp rel32
//   constexpr char INST_RELATIVE_JMP[] = "\xe9\xff\xff\xff\xff";
//   // lea rax, [rip + offset]
//   constexpr char INST_LEA_RAX_DEREF_RIP_PLUS_OFFSET[] = "\x48\x8d\x05\xff\xff\xff\xff";
//   // mov [rsp], rax
//   constexpr char INST_MOV_DEREF_RSP_RAX[] = "\x48\x89\x04\x24";
//   // ret
//   constexpr char INST_RET[] = "\xc3";
//
//   ClearDataPage();
//   InitializeCodePage(codepage_no);
//
//   // prolog
//   AddProlog(codepage_no);
//   AddSerializeInstructionToCodePage(codepage_no);
//
//   // reset microarchitectural state sequence
//   // if the number is higher we need to make sure that we have enough "unimportet guardian" stack space
//   assert(reset_executions_amount <= 100);
//   for (int i = 0; i < reset_executions_amount; i++) {
//     AddInstructionToCodePage(codepage_no, reset_sequence);
//   }
//   AddSerializeInstructionToCodePage(codepage_no);
//
//
//   //
//   // use spectre-RSB to speculatively execute the trigger
//   //
//
//   // note that for all following calculations sizeof has the additional '\0', hence the - 1
//   // we use this to generate a call which can be misprecided; target is behind the speculated code
//   int32_t call_displacement = trigger_sequence.size() + sizeof(INST_RELATIVE_JMP) - 1;
//   // we use this to redirect speculation to the same end as the manipulated stack
//   int32_t jmp_displacement = sizeof(INST_LEA_RAX_DEREF_RIP_PLUS_OFFSET) - 1 +
//       sizeof(INST_MOV_DEREF_RSP_RAX) - 1 + sizeof(INST_RET) - 1;
//   // we use this to generate the actual address where we return and replace
//   // the saved rip on the stack before calling RET
//   int32_t lea_rip_displacement = sizeof(INST_MOV_DEREF_RSP_RAX) - 1 +
//       sizeof(INST_RET) - 1;
//
//   byte_array jmp_displacement_encoded = NumberToBytesLE(jmp_displacement, 4);
//   byte_array call_displacement_encoded = NumberToBytesLE(call_displacement, 4);
//   byte_array lea_rip_displacement_encoded = NumberToBytesLE(lea_rip_displacement, 4);
//
//   // only place opcode and add offset manually
//   AddInstructionToCodePage(codepage_no, INST_RELATIVE_CALL, 1);
//   AddInstructionToCodePage(codepage_no, call_displacement_encoded);
//
//
//   // speculation starts here as return address is mispredicted
//   AddInstructionToCodePage(codepage_no, trigger_sequence);
//   // this is still only accessible during speculation to redirect speculation to the correct jumpout
//   // only place opcode and add offset manually
//   AddInstructionToCodePage(codepage_no, INST_RELATIVE_JMP, 1);
//   AddInstructionToCodePage(codepage_no, jmp_displacement_encoded);
//   //
//   // speculation ends here
//   //
//
//
//   // Target of CALL_DISPLACEMENT
//   // change the return address on the stack to trigger the missspeculation of the RET
//   // only place opcode and add offset manually
//   AddInstructionToCodePage(codepage_no, INST_LEA_RAX_DEREF_RIP_PLUS_OFFSET,
//                                       3);
//   AddInstructionToCodePage(codepage_no, lea_rip_displacement_encoded);
//   // wanted return address is now in RAX hence we can manipulate the stack now
//   AddInstructionToCodePage(codepage_no, INST_MOV_DEREF_RSP_RAX,
//                                       4);
//   // return address was manipulated hence RET will return to the correct code but
//   // will be mispredicted
//   AddInstructionToCodePage(codepage_no, INST_RET, 1);
//
//   // target of LEA_RIP_DISPLACEMENT (manipulated RET) and JMP_DISPLACEMENT
//   // serialize after trigger
//   //page_idx = AddSerializeInstructionToCodePage(page_idx, codepage_no);
//
//   // time measurement sequence
//   AddTimerStartToCodePage(codepage_no);
//   AddInstructionToCodePage(codepage_no, measurement_sequence);
//   AddTimerEndToCodePage(codepage_no);
//
//   // return timing result and epilog
//   MakeTimerResultReturnValue(codepage_no);
//   AddEpilog(codepage_no);
//
//   // make sure that we do not exceed page boundaries
//   assert(code_pages_last_written_index_[codepage_no] < kPagesize);
// }
//
// int Executor::ExecuteTestrun(int codepage_no, uint64_t* cycles_elapsed) {
//   return ExecuteCodePage(execution_code_pages_[codepage_no], cycles_elapsed);
// }
//
// void Executor::ClearDataPage() {
//   for (const auto& datapage : execution_data_pages_) {
//     memset(datapage, '\0', kPagesize);
//   }
// }
//
//
//
//
//
//   //初始化指定的代码页
//   void Executor::InitializeCodePage(int codepage_no) {
//   // ARM64 NOP 和 RET 指令的机器码
//   constexpr uint32_t INST_RET = 0xD65F03C0; // RET
//   constexpr uint32_t INST_NOP = 0xD503201F; // NOP
//
//   assert(codepage_no < static_cast<int>(execution_code_pages_.size()));
//
//   // 填充 NOP 指令
//   uint32_t* page = reinterpret_cast<uint32_t*>(execution_code_pages_[codepage_no]);
//   size_t num_nops = kPagesize / sizeof(uint32_t);
//   for (size_t i = 0; i < num_nops; ++i) {
//     page[i] = INST_NOP;
//   }
//
//   // 添加 RET 指令作为最后一条指令
//   page[num_nops - 1] = INST_RET;
//
//   // 重置写入索引
//   code_pages_last_written_index_[codepage_no] = 0;
// }
//
//
//
//
//
//
// void Executor::AddEpilog(int codepage_no) {
//
//
//         constexpr char INST_LDR_X29_X30_SP[] = "\xfd\x7b\xc3\xa8";
//         AddInstructionToCodePage(codepage_no, INST_LDR_X29_X30_SP, sizeof(INST_LDR_X29_X30_SP) - 1);
//         // constexpr char INST_LDR_X28[] = "\xFF\x23\x00\x91"; // ff 23 00 91
//         // constexpr char INST_ADD_SP_8[] = "\xFC\x87\x40\xF8"; // fc 87 40 f8
//         // constexpr char INST_LDP_X26_X27[] = "\xFA\x6F\xC1\xA8"; // fa 6f c1 a8
//         // constexpr char INST_LDP_X24_X25[] = "\xF8\x67\xC1\xA8"; // f8 67 c1 a8
//         // constexpr char INST_LDP_X22_X23[] = "\xF6\x5F\xC1\xA8"; // f6 5f c1 a8
//         // constexpr char INST_LDP_X20_X21[] = "\xF4\x57\xC1\xA8"; // f4 57 c1 a8
//         // constexpr char INST_LDP_X18_X19[] = "\xF2\x4F\xC1\xA8"; // f2 4f c1 a8
//         // constexpr char INST_LDP_X16_X17[] = "\xF0\x47\xC1\xA8"; // f0 47 c1 a8
//         // constexpr char INST_LDP_X14_X15[] = "\xEE\x3F\xC1\xA8"; // ee 3f c1 a8
//         // constexpr char INST_LDP_X12_X13[] = "\xEC\x37\xC1\xA8"; // ec 37 c1 a8
//         // constexpr char INST_LDP_X10_X11[] = "\xEA\x2F\xC1\xA8"; //ea 2f c1 a8
//         // constexpr char INST_LDP_X8_X9[] = "\xE8\x27\xC1\xA8"; //e8 27 c1 a8
//         // constexpr char INST_LDP_X29_30[] = "\xFD\x7B\xC1\xA8"; // fd 7b c1 a8
//         // constexpr char INST_MOV_SP_X29[] = "\xbf\x03\x00\x91"; // fd 7b c1 a8 "mov sp,X29\n"//bf 03 00 91
//
//         // AddInstructionToCodePage(codepage_no, INST_MOV_SP_X29, sizeof(INST_MOV_SP_X29) - 1);
//         //
//         // AddInstructionToCodePage(codepage_no, INST_ADD_SP_8, sizeof(INST_ADD_SP_8) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDR_X28, sizeof(INST_LDR_X28) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X26_X27, sizeof(INST_LDP_X26_X27) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X24_X25, sizeof(INST_LDP_X24_X25) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X22_X23, sizeof(INST_LDP_X22_X23) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X20_X21, sizeof(INST_LDP_X20_X21) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X18_X19, sizeof(INST_LDP_X18_X19) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X16_X17, sizeof(INST_LDP_X16_X17) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X14_X15, sizeof(INST_LDP_X14_X15) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X12_X13, sizeof(INST_LDP_X12_X13) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X10_X11, sizeof(INST_LDP_X10_X11) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X8_X9, sizeof(INST_LDP_X8_X9) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_LDP_X29_30, sizeof(INST_LDP_X29_30) - 1);
//
//         constexpr char INST_RET[] = "\xC0\x03\x5F\xD6"; // ret
//         AddInstructionToCodePage(codepage_no, INST_RET, sizeof(INST_RET) - 1);
// }
//
//  void Executor::AddProlog(int codepage_no) {
//
//         constexpr char INST_STP_X29_X30_SP_48[] = "\xFd\x7b\xbd\xa9"; // fd 03 00 91
//         constexpr char INST_MOV_X29_SP[] = "\xfd\x03\x00\x91"; // fd 03 00 91
//   AddInstructionToCodePage(codepage_no, INST_STP_X29_X30_SP_48, sizeof(INST_STP_X29_X30_SP_48) - 1);
//   AddInstructionToCodePage(codepage_no, INST_MOV_X29_SP, sizeof(INST_MOV_X29_SP) - 1);
//
//         // constexpr char INST_MOV_X29_SP[] = "\xFd\x03\x00\x91"; // fd 03 00 91
//         // constexpr char INST_STP_X29_X30[] = "\xfd\x7B\xBF\xA9"; //fd 7b bf a9
//         // constexpr char INST_STP_X8_X9[] = "\xe8\x27\xBF\xA9"; // e8 27 bf a9
//         // constexpr char INST_STP_X10_X11[] = "\xea\x2f\xBF\xA9"; //ea 2f bf a9
//         // constexpr char INST_STP_X12_X13[] = "\xec\x37\xBF\xA9"; // ec 37 bf a9
//         // constexpr char INST_STP_X14_X15[] = "\xee\x3f\xBF\xA9"; //ee 3f bf a9
//         // constexpr char INST_STP_X16_X17[] = "\xf0\x47\xBF\xA9"; // f0 47 bf a9
//         // constexpr char INST_STP_X18_X19[] = "\xf2\x4f\xBF\xA9"; // f2 4f bf a9
//         // constexpr char INST_STP_X20_X21[] = "\xf4\x57\xBF\xA9"; // f4 57 bf a9
//         // constexpr char INST_STP_X22_X23[] = "\xf6\x5f\xBF\xA9"; // f6 5f bf a9
//         // constexpr char INST_STP_X24_X25[] = "\xf8\x67\xBF\xA9"; // f8 67 bf a9
//         // constexpr char INST_STP_X26_X27[] = "\xfa\x6f\xBF\xA9"; // fa 6f bf a9
//         // constexpr char INST_STP_X28[] = "\xfc\x8f\x1F\xf8"; // fc 8f 1f f8
//         // constexpr char INST_SUB_SP_8[] = "\xff\x23\x00\xd1"; // ff 23 00 d1
//         //
//         //
//         // constexpr char INST_SP_16[] = "\xff\x23\x00\xd1"; // ff 23 00 d1
//         // //constexpr char INST_MOV_X29_SP[] = "\xfd\x03\x00\x91"; // ff 23 00 d1
//         // constexpr char INST_SUB_SP_1000[] = "\xff\x07\x40\xd1"; // ff 23 00 d1
//         //
//         // //X9-X15 是调用者保存的寄存器,X19-X28是被调用者保存的寄存器,我们在程序中使用X19和X20
//         // //这里我保存了（X29）FP和（X30）LR，
//         // AddInstructionToCodePage(codepage_no, INST_STP_X29_X30, sizeof(INST_STP_X29_X30) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_MOV_X29_SP, sizeof(INST_MOV_X29_SP) - 1);
//
//         // AddInstructionToCodePage(codepage_no, INST_STP_X19_X20, sizeof(INST_STP_X16_X17) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_STP_X21_X22, sizeof(INST_STP_X18_X19) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_STP_X23_X24, sizeof(INST_STP_X20_X21) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_STP_X25_X26, sizeof(INST_STP_X22_X23) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_STP_X27_X28, sizeof(INST_STP_X24_X25) - 1);
//         //
//         //
//         //
//         //
//         // AddInstructionToCodePage(codepage_no, INST_MOV_X29_SP, sizeof(INST_MOV_X29_SP) - 1);
//         // AddInstructionToCodePage(codepage_no, INST_SUB_SP_1000, sizeof(INST_SUB_SP_1000) - 1);
//         // //默认寄存器 X0,X1
//         // constexpr char INST_MOV_X[] = "\x00\x00\x80\xd2"; // 00 00 80 d2
//         // constexpr char INST_MOV_X[] = "\x01\x00\x80\xd2"; // 01 00 80 d2
//         //AddInstructionToCodePage(codepage_no, INST_MOV_X0, sizeof(INST_MOV_X0) - 1);
//         //AddInstructionToCodePage(codepage_no, INST_MOV_X1, sizeof(INST_MOV_X1) - 1);
//     }
//
// void Executor::AddSerializeInstructionToCodePage(int codepage_no) {
//   // insert CPUID to serialize instruction stream
//   constexpr char INST_DSB_SY[] = "\x9f\x3f\x03\xd5"; // DSB SY
//   constexpr char INST_ISB_SY[] = "\xdf\x3f\x03\xd5"; // ISB SY
//   constexpr char INST_DMB_SY[] = "\xbf\x3f\x03\xd5";
//   AddInstructionToCodePage(codepage_no, INST_DSB_SY, sizeof(INST_DSB_SY)-1);
//   AddInstructionToCodePage(codepage_no, INST_ISB_SY, sizeof(INST_ISB_SY)-1);
//   AddInstructionToCodePage(codepage_no, INST_DMB_SY, sizeof(INST_DMB_SY)-1);
//
// }
//
// //code for arm,maybe need to recode
//   void Executor::AddTimerStartToCodePage(int codepage_no) {
//   // 定义 ARM 指令的机器码（AArch64，小端序）
//   //DSB DSB 设置内存屏障，确保在执行计时前的所有指令都已完成
//
//   //逻辑上 先把X9 置为 0
//   //读取PMCCNTR_EL0到X9
//   // 然后X9移到X10,time 在X10中
//   constexpr char INST_DMB_SY[] = "\xbf\x3f\x03\xd5";              // DMB SY
//   constexpr char INST_ISB_SY[] = "\xdF\x3f\x03\xd5";// ISB SY
//   constexpr char INST_DSB[] = "\x9f\x3f\x03\xd5";    // DSB SY
//   constexpr char INST_MOV_X19_0[] = "\x33\x00\x80\xd2";          // MOV X9, #0  09 00 80 d2
//   constexpr char INST_MRS_X19_PMCCNTR_EL0[] = "\x13\x9d\x3b\xd5"; // MRS X9, PMCCNTR_EL0 09 9d 3b d5
//   constexpr char INST_MOV_X20_X19[] = "\xF4\x03\x13\xaa";          // MOV X10, X9 ea 03 09 aa
//
//   // 添加 ARM 指令到代码页，每条指令长度为 4 字节
//   AddInstructionToCodePage(codepage_no, INST_DSB, sizeof(INST_DSB)-1);
//   AddInstructionToCodePage(codepage_no, INST_ISB_SY, sizeof(INST_ISB_SY)-1);
//   AddInstructionToCodePage(codepage_no, INST_DMB_SY, sizeof(INST_DMB_SY)-1);
//   AddInstructionToCodePage(codepage_no, INST_MOV_X19_0, sizeof(INST_MOV_X19_0)-1);
//   AddInstructionToCodePage(codepage_no, INST_MRS_X19_PMCCNTR_EL0, sizeof(INST_MRS_X19_PMCCNTR_EL0)-1);
//   AddInstructionToCodePage(codepage_no, INST_MOV_X20_X19, sizeof(INST_MOV_X20_X19)-1);
// }
//
//   void Executor::MakeTimerResultReturnValue(int codepage_no) {
//   constexpr char INST_STR_X0_SP_SP_40[] = "\xE0\x17\x00\xf9";  // MOV X0, X15, e0 03 0f aa
//   constexpr char INST_LDR_X0_SP_40[] = "\xe0\x17\x40\xF9";
//   assert(code_pages_last_written_index_[codepage_no] + 2*(sizeof(INST_STR_X0_SP_SP_40)-1 )< kPagesize);
//   AddInstructionToCodePage(codepage_no, INST_STR_X0_SP_SP_40, sizeof(INST_STR_X0_SP_SP_40) -1 );
//   AddInstructionToCodePage(codepage_no, INST_LDR_X0_SP_40, sizeof(INST_LDR_X0_SP_40) -1 );
//   // constexpr char INST_RET[] = "\xC0\x03\x5F\xD6"; // ret
//   // AddInstructionToCodePage(codepage_no, INST_RET, sizeof(INST_RET) - 1);
//
// }
//
//   //code for arm,maybe need to recode
//   void Executor::AddTimerEndToCodePage(int codepage_no) {
//
//   constexpr char INST_DMB_SY[] = "\xbf\x3f\x03\xd5";              // DMB SY
//   constexpr char INST_ISB_SY[] = "\xdF\x3f\x03\xd5";// ISB SY
//   constexpr char INST_DSB[] = "\x9f\x3f\x03\xd5";    // DSB SY
//   constexpr char INST_MOV_X19_0[] = "\x13\x00\x80\xd2";          // MOV X9, #0  09 00 80 d2
//   constexpr char INST_MRS_X19_PMCCNTR_EL0[] = "\x13\x9d\x3b\xd5"; // MRS X9, PMCCNTR_EL0 09 9d 3b d5
//   constexpr char INST_SUB_X0_X9_X20[] = "\x60\x02\x14\xcb";  // SUB X15, X9, X10 20 01 0a cbC
//
//
//   // 添加 ARM 指令到代码页，每条指令长度为 4 字节
//
//   AddInstructionToCodePage(codepage_no, INST_MOV_X19_0, sizeof(INST_MOV_X19_0)-1);                // MOV X0, #0
//   AddInstructionToCodePage(codepage_no, INST_MRS_X19_PMCCNTR_EL0, sizeof(INST_MRS_X19_PMCCNTR_EL0)-1);                  // ISB SY
//   AddInstructionToCodePage(codepage_no, INST_SUB_X0_X9_X20, sizeof(INST_SUB_X0_X9_X20)-1);
//
//
//   AddInstructionToCodePage(codepage_no, INST_DSB, sizeof(INST_DSB)-1);
//   AddInstructionToCodePage(codepage_no, INST_ISB_SY, sizeof(INST_ISB_SY)-1);
//   AddInstructionToCodePage(codepage_no, INST_DMB_SY, sizeof(INST_DMB_SY)-1);
//   // MRS X0, PMCCNTR_EL0
// }
//
//
//
//   //将指令写入代码页的作用主要在于支持 动态代码生成 和 运行时执行，通过这种方式，程序可以根据运行时环境动态生成、调整并执行代码，从而实现更高的性能和灵活性。
// void Executor::AddInstructionToCodePage(int codepage_no,
//                                           const char* instruction_bytes,
//                                           size_t instruction_length) {
//   size_t page_idx = code_pages_last_written_index_[codepage_no];
//   if (page_idx + instruction_length >= kPagesize) {
//     std::stringstream sstream;
//     sstream << "Problematic code page is at address 0x"
//             << std::hex << reinterpret_cast<int64_t>(execution_code_pages_[codepage_no]);
//     LOG_DEBUG(sstream.str());
//     LOG_ERROR("Generated code exceeds page boundary");
//     std::abort();
//   }
//
//   assert(codepage_no < static_cast<int>(execution_code_pages_.size()));
//   memcpy(execution_code_pages_[codepage_no] + page_idx, instruction_bytes, instruction_length);
//   code_pages_last_written_index_[codepage_no] = page_idx + instruction_length;
// }
//
//   //overload
// void Executor::AddInstructionToCodePage(int codepage_no,
//                                           const byte_array& instruction_bytes) {
//   size_t page_idx = code_pages_last_written_index_[codepage_no];
//   if (page_idx + instruction_bytes.size() >= kPagesize) {
//     std::stringstream sstream;
//     sstream << "Problematic code page is at address 0x"
//       << std::hex << reinterpret_cast<int64_t>(execution_code_pages_[codepage_no]);
//     LOG_DEBUG(sstream.str());
//     LOG_ERROR("Generated code exceeds page boundary (" +
//       std::to_string(page_idx + instruction_bytes.size()) + "/" + std::to_string(kPagesize) + ")");
//     std::abort();
//   }
//
//   size_t new_page_idx = page_idx;
//   assert(codepage_no < static_cast<int>(execution_code_pages_.size()));
//   for (std::byte b : instruction_bytes) {
//     execution_code_pages_[codepage_no][new_page_idx] = std::to_integer<int>(b);
//     new_page_idx++;
//   }
//   code_pages_last_written_index_[codepage_no] = new_page_idx;
// }
//
//
//   byte_array Executor::CreateSequenceOfNOPs(size_t length) {
//   //constexpr uint32_t ARM64_NOP = 0xD503201F;
//   byte_array nops;
//   constexpr auto ARM64_NOP_0 = static_cast<unsigned char>(0xD5);
//   constexpr auto ARM64_NOP_1 = static_cast<unsigned char>(0x03);
//   constexpr auto ARM64_NOP_2 = static_cast<unsigned char>(0x20);
//   constexpr auto ARM64_NOP_3 = static_cast<unsigned char>(0x1F);
//   std::byte nop_byte0{ARM64_NOP_0};
//   std::byte nop_byte1{ARM64_NOP_1};
//   std::byte nop_byte2{ARM64_NOP_2};
//   std::byte nop_byte3{ARM64_NOP_3};
//   for (size_t i = 0; i < length; i+=4) {
//     nops.push_back(nop_byte0);
//     nops.push_back(nop_byte1);
//     nops.push_back(nop_byte2);
//     nops.push_back(nop_byte3);
//   }
//
//   return nops;
// }
//
// //
// // fault handling logic
// //
// static jmp_buf fault_handler_jump_buf;
//
// // Fault counters
// static int sigsegv_no = 0;
// static int sigfpe_no = 0;
// static int sigill_no = 0;
// static int sigtrap_no = 0;
//
// void Executor::PrintFaultCount() {
//   std::cout << "=== Faultcounters of Executor ===" << std::endl
//             << "\tSIGSEGV: " << sigsegv_no << std::endl
//             << "\tSIGFPE: " << sigfpe_no << std::endl
//             << "\tSIGILL: " << sigill_no << std::endl
//             << "\tSIGTRAP: " << sigtrap_no << std::endl
//             << "=================================" << std::endl;
// }
//   void Executor::FaultHandler(int sig) {
//   // NOTE: this function and Executor::ExecuteCodePage must both be static functions
//   //       for the signal handling + jmp logic to work
//   switch (sig) {
//   case SIGSEGV:sigsegv_no++;
//     break;
//   case SIGFPE:sigfpe_no++;
//     break;
//   case SIGILL:sigill_no++;
//     break;
//   case SIGTRAP:sigtrap_no++;
//     break;
//   default:std::abort();
//   }
//
//   // jump back to the previously stored fallback point
//   longjmp(fault_handler_jump_buf, 1);
// }
//
// template<size_t size>
// void Executor::RegisterFaultHandler(std::array<int, size> signals_to_handle) {
//   for (int sig : signals_to_handle) {
//     signal(sig, Executor::FaultHandler);
//   }
// }
//  void ScanCodePage(void* codepage, size_t size) {
//   uint8_t* byte_ptr = (uint8_t*)codepage;
//   for (size_t i = 0; i < size; i+=4) {
//     printf(" 0x%02x%02x%02x%02x \n", byte_ptr[i],byte_ptr[i+1],byte_ptr[i+2],byte_ptr[i+3]);
//   }
// }
// template<size_t size>
// void Executor::UnregisterFaultHandler(std::array<int, size> signals_to_handle) {
//   for (int sig : signals_to_handle) {
//     signal(sig, SIG_DFL);
//   }
// }void flush_instruction_cache(void* addr, size_t size) {
//   // 清理并无效化指令缓存行
//   __asm__ volatile(
//       "dc civac, %0\n"
//       "isb\n"
//       :
//       : "r"(addr)
//       : "memory"
//   );
// }
//   //是一个动态代码执行的框架，能够安全地运行生成的代码页，同时测量其执行时间，并捕获运行时异常 need to change
//   //arm需要修改
// __attribute__((no_sanitize("address")))
//   int Executor::ExecuteCodePage(void* codepage, uint64_t* cycles_elapsed) {
//   /// NOTE: this function and Executor::FaultHandler must both be static functions
// ///       for the signal handling + jmp logic to work
//
//
// #if DEBUGMODE == 1
//   // list of signals that we catch and throw as errors
//   // (without DEBUGMODE the array is defined in the error case)
//   std::array<int, 4> signals_to_handle = {SIGSEGV, SIGILL, SIGFPE, SIGTRAP};
//   // register fault handler (if not in debugmode we do this in constructor/destructor as
//   //    this has a huge impact on the runtime)
//   RegisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
// #endif
//
//   if (!setjmp(fault_handler_jump_buf)) {
//     // jump to codepage
//     // 更新函数指针
//     //flush_instruction_cache(codepage, sizeof(kPagesize));
//
//     // // 使用内存屏障确保更新完成
//     // __asm__ volatile("ic iallu" ::: "memory");
//     // __asm__ volatile("isb" ::: "memory"); // 确保所有缓存一致性
//
//
//     uint64_t cycle_diff = ((uint64_t(*)()) codepage)();
//     // set return argument
//     *cycles_elapsed = cycle_diff;
//    // std::cout<<"in cycles_elapsed"<<*cycles_elapsed<<std::endl;
//
// #if DEBUGMODE == 1
//     // unregister signal handler (if not in debugmode we do this in constructor/destructor as
//     // this has a huge impact on the runtime)
//     UnregisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
// #endif
//
//     return 0;
//   } else {
//     //ScanCodePage(codepage, kPagesize);
//     // if we reach this; the code has caused a fault
//
//     // unmask the signal again as we reached this point directly from the signal handler
// #if DEBUGMODE == 0
//     // only allocate the array in case of an error to safe execution time
//     // list of signals that we catch and throw as errors
//     std::array<int, 4> signals_to_handle = {SIGSEGV, SIGILL, SIGFPE, SIGTRAP};
// #endif
//
//     sigset_t signal_set;
//     sigemptyset(&signal_set);
//     for (int sig : signals_to_handle) {
//       sigaddset(&signal_set, sig);
//     }
//     sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
//
// #if DEBUGMODE == 1
//     // unregister signal handler (if not in debugmode we do this in constructor/destructor as
//     // this has a huge impact on the runtime)
//     UnregisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
// #endif
//
//     // report that we crashed
//     *cycles_elapsed = -1;
//     return 1;
//   }
// }
//
// }  // namespace osiris
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
#include <arm_neon.h>   // 一些 ARM 架构特定的内置函数和数据类型

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
  std::cout<<"size="<<trigger_sequence.size()<<std::endl;
  byte_array nop_sequence = CreateSequenceOfNOPs(trigger_sequence.size());

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
    //__sync_synchronize();
    //std::cout<<&cycles_elapsed_trigger<<std::endl;
    asm volatile ("dsb sy");  // 执行数据同步屏障
    asm volatile ("isb");     // 执行指令同步屏障
    asm volatile("SVC #0");
    int error = ExecuteTestrun(0, &cycles_elapsed_trigger);
    asm volatile ("dsb sy");  // 执行数据同步屏障
    asm volatile ("isb");     // 执行指令同步屏障
    asm volatile("SVC #0");
    if (error) {
      // abort
      *cycles_difference = -1;
      return 1;
    }
    //std::cout<<"out cycles_elapsed"<<cycles_elapsed_trigger<<std::endl;
    if (cycles_elapsed_trigger <= 5000) {
      results_trigger.emplace_back(cycles_elapsed_trigger);
    }

  }

//0x0000515000000580


  for (int i = 0; i < no_testruns; i++) {
    // get timing without trigger sequence
    uint64_t cycles_elapsed_notrigger ;

    asm volatile ("dsb sy");  // 执行数据同步屏障
    asm volatile ("isb");     // 执行指令同步屏障
    asm volatile("SVC #0");
    int error = ExecuteTestrun(1, &cycles_elapsed_notrigger);
    asm volatile ("dsb sy");  // 执行数据同步屏障
    asm volatile ("isb");     // 执行指令同步屏障
    asm volatile("SVC #0");
    if (error) {
      // abort
      *cycles_difference = -1;
      return 1;
    }
    if (cycles_elapsed_notrigger <= 5000) {
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
  AddProlog(codepage_no);
  AddSerializeInstructionToCodePage(codepage_no);

  // first sequence
  // if we need more we also have to increase the guardian stack space

   assert(first_sequence_executions_amount <= 100);
   for (int i = 0; i < first_sequence_executions_amount; i++) {
     AddInstructionToCodePage(codepage_no, first_sequence);
   }
  AddSerializeInstructionToCodePage(codepage_no);

  // // second sequence
  AddInstructionToCodePage(codepage_no, second_sequence);
  AddSerializeInstructionToCodePage(codepage_no);
  //
  // // time measurement sequence
   AddTimerStartToCodePage(codepage_no);
   AddInstructionToCodePage(codepage_no, measurement_sequence);
  AddTimerEndToCodePage(codepage_no);

  // return timing result and epilog
  MakeTimerResultReturnValue(codepage_no);
  AddEpilog(codepage_no);

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
  assert(execution_code_pages_[codepage_no] != nullptr);
  asm volatile ("dsb sy");  // 执行数据同步屏障
  asm volatile ("isb");     // 执行指令同步屏障
  asm volatile("SVC #0");
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






void Executor::AddEpilog(int codepage_no) {

        constexpr char INST_DSB_SY[] = "\x9f\x3f\x03\xd5"; // DSB SY
        constexpr char INST_ISB_SY[] = "\xdf\x3f\x03\xd5"; // ISB SY
        //constexpr char INST_DMB_SY[] = "\xbf\x3f\x03\xd5";

        AddInstructionToCodePage(codepage_no, INST_DSB_SY, sizeof(INST_DSB_SY) - 1);
        AddInstructionToCodePage(codepage_no, INST_ISB_SY, sizeof(INST_ISB_SY) - 1);
        constexpr char INST_SVC_0[] = "\x01\x00\x00\xd4"; // ISB SY
        AddInstructionToCodePage(codepage_no, INST_SVC_0, sizeof(INST_SVC_0) - 1);
        //AddInstructionToCodePage(codepage_no, INST_DMB_SY, sizeof(INST_DMB_SY)-1);


        constexpr char INST_SP_X29[] = "\xbF\x03\x00\x91";//bf 03 00 91

        //constexpr char INST_ADD_SP_1000[] = "\xFF\x07\x40\x91"; // fc 87 40 f8
        constexpr char INST_LDR_Q15[] = "\xEF\x07\xC1\x3C"; // fa 6f c1 a8
        constexpr char INST_LDR_Q14[] = "\xEE\x07\xC1\x3C"; // fa 6f c1 a8
        constexpr char INST_LDR_Q13[] = "\xED\x07\xC1\x3C"; // fa 6f c1 a8
        constexpr char INST_LDR_Q12[] = "\xEC\x07\xC1\x3C"; // fa 6f c1 a8
        constexpr char INST_LDR_Q11[] = "\xEB\x07\xC1\x3C"; // fa 6f c1 a8
        constexpr char INST_LDR_Q10[] = "\xEA\x07\xC1\x3C"; // fa 6f c1 a8
        constexpr char INST_LDR_Q9[] = "\xE9\x07\xC1\x3C"; // fa 6f c1 a8
        constexpr char INST_LDR_Q8[] = "\xE8\x07\xC1\x3C"; // fa 6f c1 a8


        constexpr char INST_LDP_X27_X28[] = "\xFB\x73\xC1\xA8"; // fa 6f c1 a8
        constexpr char INST_LDP_X25_X26[] = "\xF9\x6B\xC1\xA8"; // f8 67 c1 a8
        constexpr char INST_LDP_X23_X24[] = "\xF7\x63\xC1\xA8"; // f6 5f c1 a8
        constexpr char INST_LDP_X21_X22[] = "\xF5\x5B\xC1\xA8"; // f4 57 c1 a8
        constexpr char INST_LDP_X19_X20[] = "\xF3\x53\xC1\xA8"; // f2 4f c1 a8

        constexpr char INST_LDP_X29_30[] = "\xFD\x7B\xC1\xA8"; // fd 7b c1 a8

        AddInstructionToCodePage(codepage_no, INST_SP_X29, sizeof(INST_SP_X29) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDR_Q15, sizeof(INST_LDR_Q15) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDR_Q14, sizeof(INST_LDR_Q14) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDR_Q13, sizeof(INST_LDR_Q13) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDR_Q12, sizeof(INST_LDR_Q12) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDR_Q11, sizeof(INST_LDR_Q11) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDR_Q10, sizeof(INST_LDR_Q10) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDR_Q9, sizeof(INST_LDR_Q9) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDR_Q8, sizeof(INST_LDR_Q8) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDP_X27_X28, sizeof(INST_LDP_X27_X28) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDP_X25_X26, sizeof(INST_LDP_X25_X26) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDP_X23_X24, sizeof(INST_LDP_X23_X24) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDP_X21_X22, sizeof(INST_LDP_X21_X22) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDP_X19_X20, sizeof(INST_LDP_X19_X20) - 1);
        AddInstructionToCodePage(codepage_no, INST_LDP_X29_30, sizeof(INST_LDP_X29_30) - 1);

        constexpr char INST_RET[] = "\xC0\x03\x5F\xD6"; // ret
        AddInstructionToCodePage(codepage_no, INST_RET, sizeof(INST_RET) - 1);
}

 void Executor::AddProlog(int codepage_no) {
        constexpr char INST_STP_X29_X30[] = "\xfd\x7B\xBF\xA9"; //fd 7b bf a9
        constexpr char INST_MOV_X29_SP[] = "\xFd\x03\x00\x91"; // fd 03 00 91

        constexpr char INST_STP_X19_X20[] = "\xf3\x53\xBF\xA9"; // f4 57 bf a9
        constexpr char INST_STP_X21_X22[] = "\xf5\x5b\xBF\xA9"; // f6 5f bf a9
        constexpr char INST_STP_X23_X24[] = "\xf7\x63\xBF\xA9"; // f8 67 bf a9
        constexpr char INST_STP_X25_X26[] = "\xf9\x6b\xBF\xA9"; // fa 6f bf a9
        constexpr char INST_STP_X27_X28[] = "\xfb\x73\xBF\xA9"; // f2 4f bf a9

        constexpr char INST_STR_Q8[] = "\xE8\x0F\x9F\x3C"; // f2 4f bf a9
        constexpr char INST_STR_Q9[] = "\xE9\x0F\x9F\x3C"; // f2 4f bf a9
        constexpr char INST_STR_Q10[] = "\xEA\x0F\x9F\x3C"; // f2 4f bf a9
        constexpr char INST_STR_Q11[] = "\xEB\x0F\x9F\x3C"; // f2 4f bf a9
        constexpr char INST_STR_Q12[] = "\xEC\x0F\x9F\x3C";  // f2 4f bf a9
        constexpr char INST_STR_Q13[] = "\xED\x0F\x9F\x3C"; ; // f2 4f bf a9
        constexpr char INST_STR_Q14[] = "\xEE\x0F\x9F\x3C"; ; // f2 4f bf a9
        constexpr char INST_STR_Q15[] = "\xEF\x0F\x9F\x3C"; ; // f2 4f bf a9

        //constexpr char INST_SUB_SP_1000[] = "\xff\x07\x40\xd1"; // ff 23 00 d1
        AddInstructionToCodePage(codepage_no, INST_STP_X29_X30, sizeof(INST_STP_X29_X30) - 1);
        AddInstructionToCodePage(codepage_no, INST_STP_X19_X20, sizeof(INST_STP_X19_X20) - 1);
        AddInstructionToCodePage(codepage_no, INST_STP_X21_X22, sizeof(INST_STP_X21_X22) - 1);
        AddInstructionToCodePage(codepage_no, INST_STP_X23_X24, sizeof(INST_STP_X23_X24) - 1);
        AddInstructionToCodePage(codepage_no, INST_STP_X25_X26, sizeof(INST_STP_X25_X26) - 1);
        AddInstructionToCodePage(codepage_no, INST_STP_X27_X28, sizeof(INST_STP_X27_X28) - 1);
        AddInstructionToCodePage(codepage_no, INST_STR_Q8, sizeof(INST_STR_Q8) - 1);
        AddInstructionToCodePage(codepage_no, INST_STR_Q9, sizeof(INST_STR_Q9) - 1);
        AddInstructionToCodePage(codepage_no, INST_STR_Q10, sizeof(INST_STR_Q10) - 1);
        AddInstructionToCodePage(codepage_no, INST_STR_Q11, sizeof(INST_STR_Q11) - 1);
        AddInstructionToCodePage(codepage_no, INST_STR_Q12, sizeof(INST_STR_Q12) - 1);
        AddInstructionToCodePage(codepage_no, INST_STR_Q13, sizeof(INST_STR_Q13) - 1);
        AddInstructionToCodePage(codepage_no, INST_STR_Q14, sizeof(INST_STR_Q14) - 1);
        AddInstructionToCodePage(codepage_no, INST_STR_Q15, sizeof(INST_STR_Q15) - 1);
        AddInstructionToCodePage(codepage_no, INST_MOV_X29_SP, sizeof(INST_MOV_X29_SP) - 1);
        //AddInstructionToCodePage(codepage_no, INST_SUB_SP_1000, sizeof(INST_SUB_SP_1000) - 1);
        //默认OP寄存器 X15,X16
        constexpr char INST_MOV_X25_0[] = "\x19\x00\x80\xd2"; // 0f 00 80 d2
        constexpr char INST_MOV_X24_0[] = "\x18\x00\x80\xd2"; // 10 00 80 d2
        AddInstructionToCodePage(codepage_no, INST_MOV_X25_0, sizeof(INST_MOV_X25_0) - 1);
        AddInstructionToCodePage(codepage_no, INST_MOV_X24_0, sizeof(INST_MOV_X24_0) - 1);
    }

void Executor::AddSerializeInstructionToCodePage(int codepage_no) {
  // insert CPUID to serialize instruction stream
  constexpr char INST_DSB_SY[] = "\x9f\x3f\x03\xd5"; // DSB SY
  constexpr char INST_ISB_SY[] = "\xdf\x3f\x03\xd5"; // ISB SY
  constexpr char INST_SVC_0[] = "\x01\x00\x00\xd4"; // ISB SY
  //01 00 00 d4
  //constexpr char INST_DMB_SY[] = "\xbf\x3f\x03\xd5";
  AddInstructionToCodePage(codepage_no, INST_DSB_SY, sizeof(INST_DSB_SY)-1);
  AddInstructionToCodePage(codepage_no, INST_ISB_SY, sizeof(INST_ISB_SY)-1);
  AddInstructionToCodePage(codepage_no, INST_SVC_0, sizeof(INST_SVC_0)-1);

}

//code for arm,maybe need to recode
  void Executor::AddTimerStartToCodePage(int codepage_no) {
  // 定义 ARM 指令的机器码（AArch64，小端序）
  //DSB DSB 设置内存屏障，确保在执行计时前的所有指令都已完成
  //逻辑上 先把X9 置为 0
  //读取PMCCNTR_EL0到X9
  // 然后X9移到X10,time 在X10中
  //constexpr char INST_DMB_SY[] = "\xbf\x3f\x03\xd5";              // DMB SY
  constexpr char INST_DSB[] = "\x9f\x3f\x03\xd5";    // DSB SY
  constexpr char INST_ISB_SY[] = "\xdF\x3f\x03\xd5";// ISB SY
  constexpr char INST_MOV_X27_0[] = "\x1b\x00\x80\xd2";          // MOV X9, #0  09 00 80 d2
  constexpr char INST_MRS_X27_PMCCNTR_EL0[] = "\x1b\x9d\x3b\xd5"; // MRS X9, PMCCNTR_EL0 09 9d 3b d5
  constexpr char INST_MOV_X26_X27[] = "\xfa\x03\x1b\xaa";          // MOV X10, X9 ea 03 09 aa

  // 添加 ARM 指令到代码页，每条指令长度为 4 字节
  AddInstructionToCodePage(codepage_no, INST_DSB, sizeof(INST_DSB)-1);
  AddInstructionToCodePage(codepage_no, INST_ISB_SY, sizeof(INST_ISB_SY)-1);
  constexpr char INST_SVC_0[] = "\x01\x00\x00\xd4"; // ISB SY
  AddInstructionToCodePage(codepage_no, INST_SVC_0, sizeof(INST_SVC_0)-1);
  //AddInstructionToCodePage(codepage_no, INST_DMB_SY, sizeof(INST_DMB_SY)-1);
  AddInstructionToCodePage(codepage_no, INST_MOV_X27_0, sizeof(INST_MOV_X27_0)-1);
  AddInstructionToCodePage(codepage_no, INST_MRS_X27_PMCCNTR_EL0, sizeof(INST_MRS_X27_PMCCNTR_EL0)-1);
  AddInstructionToCodePage(codepage_no, INST_MOV_X26_X27, sizeof(INST_MOV_X26_X27)-1);
}

  void Executor::MakeTimerResultReturnValue(int codepage_no) {
  constexpr char INST_MOV_X0_15[] = "\xe0\x03\x0f\xaa";  // MOV X0, X15, e0 03 0f aa
  assert(code_pages_last_written_index_[codepage_no] + sizeof(INST_MOV_X0_15)-1 < kPagesize);
  AddInstructionToCodePage(codepage_no, INST_MOV_X0_15, sizeof(INST_MOV_X0_15) -1 );

}

  //code for arm,maybe need to recode
  void Executor::AddTimerEndToCodePage(int codepage_no) {

  //constexpr char INST_DMB_SY[] = "\xbf\x3f\x03\xd5";              // DMB SY
  constexpr char INST_ISB_SY[] = "\xdF\x3f\x03\xd5";// ISB SY
  constexpr char INST_DSB[] = "\x9f\x3f\x03\xd5";    // DSB SY
  //constexpr char INST_MOV_X27_0[] = "\x1b\x00\x80\xd2";          // MOV X9, #0  09 00 80 d2
  constexpr char INST_MRS_X27_PMCCNTR_EL0[] = "\x1b\x9d\x3b\xd5"; // MRS X9, PMCCNTR_EL0 09 9d 3b d5
  constexpr char INST_SUB_X15_X27_X26[] = "\x6f\x03\x1a\xcb";  // SUB X15, X9, X10 20 01 0a cb
  // 添加 ARM 指令到代码页，每条指令长度为 4 字节

  //AddInstructionToCodePage(codepage_no, INST_MOV_X27_0, sizeof(INST_MOV_X27_0)-1);                // MOV X0, #0
  AddInstructionToCodePage(codepage_no, INST_MRS_X27_PMCCNTR_EL0, sizeof(INST_MRS_X27_PMCCNTR_EL0)-1);                  // ISB SY
  AddInstructionToCodePage(codepage_no, INST_SUB_X15_X27_X26, sizeof(INST_SUB_X15_X27_X26)-1);


  AddInstructionToCodePage(codepage_no, INST_DSB, sizeof(INST_DSB)-1);
  AddInstructionToCodePage(codepage_no, INST_ISB_SY, sizeof(INST_ISB_SY)-1);
  constexpr char INST_SVC_0[] = "\x01\x00\x00\xd4"; // ISB SY
  AddInstructionToCodePage(codepage_no, INST_SVC_0, sizeof(INST_SVC_0)-1);
  //AddInstructionToCodePage(codepage_no, INST_DMB_SY, sizeof(INST_DMB_SY)-1);
  // MRS X0, PMCCNTR_EL0
}



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
    // 更新函数指针
    //flush_instruction_cache(codepage, sizeof(kPagesize));

    // // 使用内存屏障确保更新完成
    // __asm__ volatile("ic iallu" ::: "memory");
    // __asm__ volatile("isb" ::: "memory"); // 确保所有缓存一致性

    asm volatile ("dsb sy");  // 执行数据同步屏障
    asm volatile ("isb");     // 执行指令同步屏障
    asm volatile("SVC #0");
    std::cout<<"start to execute codepage"<<std::endl;
    uint64_t cycle_diff = ((uint64_t(*)()) codepage)();
    //codepage  = codepage1;
    // set return argument
    asm volatile ("dsb sy");  // 执行数据同步屏障
    asm volatile ("isb");     // 执行指令同步屏障
    asm volatile("SVC #0");
    *cycles_elapsed = cycle_diff;
   // std::cout<<"in cycles_elapsed"<<*cycles_elapsed<<std::endl;

#if DEBUGMODE == 1
    // unregister signal handler (if not in debugmode we do this in constructor/destructor as
    // this has a huge impact on the runtime)
    UnregisterFaultHandler<signals_to_handle.size()>(signals_to_handle);
#endif

    return 0;
  } else {
    //ScanCodePage(codepage, kPagesize);
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