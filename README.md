# 关于“帝江”
“帝江”是一个通信框架，底层可以使用TCP方式或者RDMA方式。

# 使用方式
## Server端
```
char port[] = "12345";
const int threadNum = 256;
RdmaServerSocket *socket = new RdmaServerSocket(port, threadNum);
auto handler = [](char *buffer, int size)
{
    fprintf(stdout, "size is : %d \n", size);
    std::string content(buffer, size);
    fprintf(stdout, "content is: %s \n", content.c_str());
};
socket->RegisterMessageCallback(handler, messageBufferSize);
socket->Loop();
```

## Client端
```
SAY("Client");
char port[] = "12345";
char ip[] = "10.0.0.28";
RdmaClientSocket *socket = new RdmaClientSocket(ip, port, 16, 10 * 1024 * 1024, 500);
char message[] = "hello, world!";
socket->Write(message, strlen(message));
socket->Loop();
```


# 名称来源
帝江，《山海经》中的神兽，人面鸟身，背有四张肉翅，胸前、腹部、双腿六爪;善速度，四翅一扇二十八万里。

![帝江](https://user-images.githubusercontent.com/56379080/147912511-a0f90093-9c03-41d7-b066-4d08d316691b.png)
