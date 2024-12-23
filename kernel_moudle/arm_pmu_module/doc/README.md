安装内核模块：
sudo insmod /home/acmachines/osiris/arm_pmu_module/arm_pmu.ko

卸载内核模块
sudo rmmod arm_pmu

检查内核模块启动
lsmod | grep arm_pmu
