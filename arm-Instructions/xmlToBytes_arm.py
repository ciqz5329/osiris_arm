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
    操作数的数量不固定，按需动态拼接所有操作数。
    """
    asm_code = instrNode.attrib['asm']
    operands = []

    # 遍历所有操作数节点
    for operandNode in instrNode.iter('operand'):
        operandType = operandNode.attrib['type']
        operandName = operandNode.attrib['name']

        if operandType == 'reg':
            # 默认使用第一个寄存器
            registers = operandNode.text.split(',')
            default_value = operandNode.attrib.get('default', None)
            reg_value = default_value if default_value else "X24"  # 默认值处理
            operands.append(reg_value)

        elif operandType == 'enum':
            # 针对移位类型的简单映射
            operandOptional = operandNode.attrib.get('Optional', '')
            if (operandName == 'shift' or operandName=='extend')and operandOptional == 'true':
                continue
            options = operandNode.findall('option')
            if options:
                value = options[0].attrib.get('value', '')
                if value in ['00', '01', '10']:
                    shift_map = {'00': 'LSL', '01': 'LSR', '10': 'ASR'}
                    operands.append(shift_map.get(value, 'LSL'))
        elif operandType == 'imm':
            operandOptional = operandNode.attrib.get('Optional', '')

            if (operandName == 'amount' or operandName=='shift') and operandOptional == 'true':
                continue
            # 获取 default 节点的默认值
            default_value = operandNode.attrib.get('default', None)

            # 如果没有提供立即数或立即数不合法，使用默认值
            imm_value = default_value if default_value else "#0"  # 默认值处理
            operands.append(imm_value)

        elif operandType == 'label':
            # 处理标签操作数，默认使用default属性值
            default_value = operandNode.attrib.get('default', 'Program Label Address')
            operands.append(default_value)

        elif operandType == 'cond':
            default_value = operandNode.attrib.get('default', 'EQ')
            operands.append(default_value)


    # 拼接所有操作数，不限制数量
    final_asm = f"{asm_code} {', '.join(operands)}".strip()

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
