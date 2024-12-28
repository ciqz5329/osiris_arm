# 示例 XML 数据
asm = "ADD"
operands = ["X0", "X1", "#5"]
condition = "EQ"

# 组合成完整的汇编指令
complete_instruction = f"{asm}{condition} {', '.join(operands)}"
print(complete_instruction)  # 输出: ADD.EQ X0, X1, #5
