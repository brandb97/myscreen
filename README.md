`myscreen`命令模仿了`screen`命令的功能，但只支持`screen`命令的部分特性。简单来说，`myscreen`是一个为了好玩而写的，简单的`screen`命令。`myscreen`支持的特性如下：

1. 新建一个窗口，并在这个窗口中运行命令行指定的命令
```
myscreen cmd arg1 arg2 ...
```

2. 按下`CTRL-a d`从当前窗口分离

3. 列出`myscreen`管理的所有窗口
```
myscreen [-l|--list]
```

4. 重新连接一个已有的窗口
```
myscreen [-a|--attach] winspec
```
