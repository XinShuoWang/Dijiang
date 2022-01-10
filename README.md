# 关于“帝江”
“帝江”是一个通信框架，底层可以使用TCP方式或者RDMA方式。

# 使用方式
## Server端
```
SAY("Server");
RdmaServerSocket *socket = new RdmaServerSocket(port, threadNum, messageBufferSize);
auto handler = [](char *buffer, int size)
{
    std::string str(buffer, size);
    fprintf(stdout, "dijinag -> size is: %d, content is: %s, \n", size, str.c_str());
};
socket->RegisterHandler(handler);
socket->Loop();
```

## Client端
```
SAY("Client");
int timeout = 500;
RdmaClientSocket *socket = new RdmaClientSocket(ip, port, threadNum, messageBufferSize, timeout);
char data[] = "hello,world";
int size = strlen(data);
socket->Write(data, size);
```

## 使用截图
![server端](https://user-images.githubusercontent.com/56379080/148733078-ae867b2c-fb79-467b-9fc4-aade24f7bd3b.png)

![client端](https://user-images.githubusercontent.com/56379080/148733112-8c04e304-73fe-47c7-9c9e-72631f1f12d3.png)


# 性能测试
## 带宽测试


## 时延测试


# 名称来源
帝江，《山海经》中的神兽，人面鸟身，背有四张肉翅，胸前、腹部、双腿六爪;善速度，四翅一扇二十八万里。

![帝江](https://user-images.githubusercontent.com/56379080/147912511-a0f90093-9c03-41d7-b066-4d08d316691b.png)
