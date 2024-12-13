import sys
import xml.etree.ElementTree as ET
from multiprocessing import Process, Queue
import base64
import os
from pwn import *

THREAD_COUNT = min(10, os.cpu_count() or 1)

class AssemblyInstruction:
    base64_byte_representation = b""

    def __init__(self, assembly_code, category, extension, isa_set):
        self.assembly_code = assembly_code.encode()
        self.category = category.encode()
        self.extension = extension.encode()
        self.isa_set = isa_set.encode()

    def get_assembly_code(self):
        return self.assembly_code.decode()

    def set_base64_encoded_byte_representation(self, byte_representation):
        self.base64_byte_representation = byte_representation

    def get_csv_representation(self, delim=b";"):
        """
        将汇编指令各字段拼接为 CSV 风格的字节串：
        base64编码的机器码;指令文本;category;extension;isa_set
        """
        assert self.base64_byte_representation != b""
        # 确保分隔符不出现在任何字段中，否则需要更复杂的转义处理
        assert delim not in self.assembly_code
        assert delim not in self.category
        assert delim not in self.extension
        assert delim not in self.isa_set

        csv_line = self.base64_byte_representation
        csv_line += delim
        csv_line += self.assembly_code
        csv_line += delim
        csv_line += self.category
        csv_line += delim
        csv_line += self.extension
        csv_line += delim
        csv_line += self.isa_set

        return csv_line

def worker_func(input_queue, output_queue):
    """
    进程 worker：读取指令并调用 pwn.asm 进行装配，
    将装配得到的机器码用 base64 编码后存储到对象。
    """
    while True:
        assembly_instruction = input_queue.get()
        asm_code = assembly_instruction.get_assembly_code()
        try:
            asm_bytes = asm(asm_code)
            asm_bytes_encoded = base64.b64encode(asm_bytes)
            assembly_instruction.set_base64_encoded_byte_representation(asm_bytes_encoded)
            output_queue.put((True, assembly_instruction))
        except pwnlib.exception.PwnlibException as e:
            print(f"Error on assembling instruction '{asm_code}'")
            print(f"Error was: {e}")
            with open("error_log.txt", "a") as log_file:
                log_file.write(f"Failed: {asm_code}\nError: {str(e)}\n")
            output_queue.put((False, None))

def parse_operands(instrNode):
    """
    解析 <instruction> 节点下的 <operand> 子节点，并组合成完整的汇编指令文本。
    这里默认只截取前三个操作数；如果实际超过 3 个，也可按需调整。
    """
    asm_code = instrNode.attrib['asm']
    operands = []
    for operandNode in instrNode.iter('operand'):
        operandType = operandNode.attrib['type']
        if operandType == 'reg':
            # 默认使用第一个寄存器
            registers = operandNode.text.split(',')
            operands.append(registers[0])
        elif operandType == 'enum':
            # 针对移位类型的简单映射
            options = operandNode.findall('option')
            if options:
                value = options[0].attrib.get('value', '')
                if value in ['00', '01', '10']:
                    shift_map = {'00': 'LSL', '01': 'LSR', '10': 'ASR'}
                    operands.append(shift_map.get(value, 'LSL'))
        elif operandType == 'imm':
            # 解析立即数范围，默认使用最小值
            range_values = operandNode.attrib.get('range', '0-0').split('-')
            imm_value = f"#{range_values[0]}"
            operands.append(imm_value)
    # 拼成最终的汇编指令文本，这里默认只取前 3 个操作数
    final_asm = f"{asm_code} {', '.join(operands[:3])}".strip()
    return final_asm

def main():
    """
    main 函数：从命令行读取输出文件名，解析 XML 并创建指令列表，
    将指令分发到多进程中处理，最终输出 CSV 文件。
    """
    if len(sys.argv) != 2:
        print(f"USAGE: {sys.argv[0]} <output-filename>")
        exit(1)

    output_fname = sys.argv[1]

    context.arch = "aarch64"

    # 解析 ARM 指令 XML 文件
    root = ET.parse('instructions_arm.xml')

    assembly_instructions = []
    for instrNode in root.iter('instruction'):
        asm_code = parse_operands(instrNode)
        print(f"Processing instruction: {asm_code}")

        category = instrNode.attrib['category']
        extension = instrNode.attrib['extension']
        isa_set = instrNode.attrib['isa-set']

        assembly_instruction = AssemblyInstruction(asm_code, category, extension, isa_set)
        assembly_instructions.append(assembly_instruction)

    # 创建多进程队列
    input_queue = Queue()
    output_queue = Queue()

    for instruction in assembly_instructions:
        input_queue.put(instruction)

    workers = []
    instructions_written = 0
    try:
        # 启动若干进程
        for _ in range(THREAD_COUNT):
            p = Process(target=worker_func, args=(input_queue, output_queue))
            p.start()
            workers.append(p)

        # 写 CSV 文件
        with open(output_fname, "wb") as fd:
            header_line = b"byte_representation;assembly_code;category;extension;isa_set"
            fd.write(header_line + b"\n")
            for _ in range(len(assembly_instructions)):
                succ, assembly_instruction = output_queue.get()
                if succ:
                    fd.write(assembly_instruction.get_csv_representation() + b"\n")
                    instructions_written += 1

    except KeyboardInterrupt:
        print("[+] Aborting due to keyboard interrupt!")
    finally:
        # 结束所有 worker 进程
        for worker in workers:
            worker.terminate()

    print(f"[+] {instructions_written}/{len(assembly_instructions)} instructions stored in {output_fname}")

if __name__ == "__main__":
    main()
