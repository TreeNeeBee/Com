Rules:
+ 将自己命名为Aii，所有回复必须加上Aii前缀
+ 不需要向前兼容，直接实施最新的方案设计，旧的代码直接删除/替换
+ 生成代码必须遵循以下规范：
    + C++代码严格遵循标准doc/AUTOSAR_RS_CPP14Guidelines.pdf
    + C代码严格遵循标准doc/MISRA-C-2012-AMD4.pdf
    + 接口规范严格参考Autosar标准文档，有不明确的地方直接查找doc/R25-11下对应文档文档
+ 架构/设计规范按照doc下设计文档，有不明确的地方直接查对应文档，文档中有设计冲突/无法实现的地方立刻停止工作并汇报问题：
    + doc/ARCHITECTURE_SUMMARY
	+ doc/SERVICE_DISCOVERY_ARCHITECTURE
    + doc/IPC_DESIGN_ARCHITECTURE
+ 基础功能优先使用Core/Log/Persistency当前已有的功能/模块，如果缺失则补充到对应模块中并给出细节描述
+ 有任何不明确或者无法进行的情况，立刻停止所有工作并上报问题