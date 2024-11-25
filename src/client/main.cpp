#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
using namespace std;
using json = nlohmann::json;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <atomic>

#include "group.hpp"
#include "user.hpp"
#include "public.h"

//记录当前系统登录的用户信息
User g_currentUser;
//记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
//记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;
//显示当前登录成功用户的基本信息
void showCurrentUserData();
//控制主菜单页面程序
bool isMainMenuRunning = false;

//用于读写线程之间的通信
sem_t rwsem;
//记录登录状态
atomic_bool g_isLoginSuccess(false);

//接收线程
void readTaskHandler(int clientfd);
//获取系统时间（聊天信息需要添加时间信息）
string getCurremtTime();
//主聊天页面程序
void mainMenu(int clientfd);

//聊天客户端程序实现，main线程用作发送线程，子线程用作接收线程
int main(int argc, char** argv)
{
    if(argc < 3)
    {
        cerr << "command invalid! example:./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    //解析通过命令行参数传递的ip和port
    char* ip = argv[1];
    uint16_t port = atoi(argv[2]);

    //创建clien端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if(clientfd == -1)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    //填写socket连接需要的server信息ip+port
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    //client和server进行连接
    if(-1 == connect(clientfd, (sockaddr*)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect serer error" << endl;
        close(clientfd);
        exit(-1);
    }

    //初始化读写线程通信用的信号量
    sem_init(&rwsem, 0, 0);

    //连接服务器成功 启动接受线程
    std::thread readTask(readTaskHandler, clientfd);//pthread_create
    readTask.detach();  //pthread_detach

    //main线程用于接收用户输入，负责发送数据
    for(;;)
    {
        //显示页面菜单 登录注册 退出
        cout << "***********************" << endl;
        cout << "       1.login" << endl;
        cout << "       2.register" << endl;
        cout << "       3.quit" << endl;
        cout << "***********************" << endl;
        cout << "choice:";
        int choice = -1;
        cin >> choice;
        cin.get();//读取缓冲区残留回车

        switch(choice)
        {
            //登录
            case 1:
            {
                int id = -1;
                char pwd[50] = {0};
                cout << "userid:";
                cin >> id;
                cin.get();//读取缓冲区残留回车
                cout << "userpassword:";
                cin.getline(pwd, 50);

                json js;
                js["msgid"] = LOGIN_MSG;
                js["id"] = id;
                js["password"] = pwd;
                string request = js.dump();

                g_isLoginSuccess = false;

                int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
                if(len == -1)
                {
                    cerr << "send login msg error" << endl;
                }

                sem_wait(&rwsem);//等待信号量 由子线程处理完登录的响应消息后 通知这里
                if(g_isLoginSuccess)
                {              
                    // 进入聊天主菜单页面  
                    isMainMenuRunning = true;
                    mainMenu(clientfd);
                }
            }
                break;
            //注册
            case 2:
            {
                char name[50] = {0};
                char pwd[50] = {0};
                cout << "username:";
                cin.getline(name, 50);
                cout << "userpassword:";
                cin.getline(pwd, 50);

                json js;
                js["msgid"] = REG_MSG;
                js["name"] = name;
                js["password"] = pwd;
                string request = js.dump();

                int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
                if(len == -1)
                {
                    cerr << "send reg msg error:" << request << endl;
                }
                sem_wait(&rwsem);//等待信号量 由子线程处理完注册的响应消息后 通知这里
            }
                break;
            case 3://退出
                close(clientfd);
                sem_destroy(&rwsem);
                exit(0);
                //break;
            default:
                cerr << "invalid input!" << endl;
                break;
        }
    }
}

//处理登录响应的业务逻辑
void doLoginResponse(json& responsejs)
{
    if (responsejs["errno"].get<int>() != 0) // 登录失败
    {
        cerr << responsejs["errmsg"] << endl;
        g_isLoginSuccess = false;
    }
    else // 登录成功
    {
        // 记录当前用户的id和name
        g_currentUser.setId(responsejs["id"].get<int>());
        g_currentUser.setName(responsejs["name"]);

        // 记录当前用户的好友列表信息
        if (responsejs.contains("friends"))
        {
            // 初始化
            g_currentUserFriendList.clear();
            vector<string> vec = responsejs["friends"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                User user;
                user.setId(js["id"].get<int>());
                user.setName(js["name"]);
                user.setState(js["state"]);
                g_currentUserFriendList.push_back(user);
            }
        }
        // 记录当前用户群组列表信息
        if (responsejs.contains("groups"))
        {
            // 初始化
            g_currentUserGroupList.clear();
            vector<string> vec1 = responsejs["groups"];
            for (string &groupstr : vec1)
            {
                json grpjs = json::parse(groupstr);
                Group group;
                group.setId(grpjs["id"].get<int>());
                group.setName(grpjs["groupname"]);
                group.setDesc(grpjs["groupdesc"]);

                vector<string> vec2 = grpjs["users"];
                ;
                for (string &userstr : vec2)
                {
                    GroupUser user;
                    json js = json::parse(userstr);
                    user.setId(js["id"].get<int>());
                    user.setName(js["name"]);
                    user.setState(js["state"]);
                    user.setRole(js["role"]);
                    group.getUsers().push_back(user);
                }
                g_currentUserGroupList.push_back(group);
            }
        }

        // 显示登录用户的基本信息
        showCurrentUserData();
        // 显示当前用户的离线消息，个人聊天消息或群组消息
        if (responsejs.contains("offlinemsg"))
        {
            vector<string> vec = responsejs["offlinemsg"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                int msgtype = js["msgid"].get<int>();
                // time + id + name + said + xxx
                if (ONE_CHAT_MSG == msgtype)
                {
                    cout << js["time"].get<string>() << "[" << js["id"] << "] " << js["name"].get<string>() << " " << "said:" << js["msg"].get<string>() << endl;
                }
                else if (GROUP_CHAT_MSG == msgtype)
                {
                    cout << "群消息[" << js["groupid"] << "]:" << js["time"].get<string>() << "[" << js["id"] << "] " << js["name"].get<string>() << " " << "said:" << js["msg"].get<string>() << endl;
                }
            }
        }
        g_isLoginSuccess = true;
    }
}

//处理注册响应的业务逻辑
void doRegResponse(json& responsejs)
{
    if (responsejs["errno"].get<int>() != 0) // 注册失败
    {
        cerr << "name is ready exist, register error!" << endl;
    }
    else // 注册成功
    {
        cout << "name register success, userid is " << responsejs["id"] << "don't forget it!" << endl;
    }
}

//子线程-接收线程
void readTaskHandler(int clientfd)
{
    for(;;)
    {
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0);//阻塞了
        if(len == -1 || len == 0)
        {
            close(clientfd);
            exit(-1);
        }

        //接受ChatServer转发的数据 反序列化
        json js = json::parse(buffer);
        int msgtype = js["msgid"].get<int>();
        if(ONE_CHAT_MSG == msgtype)
        {
            cout << js["time"].get<string>() << "[" << js["id"] << "] " << js["name"].get<string>() << " " <<
                    "said:" << js["msg"].get<string>() << endl;
            continue;
        }
        
        if(GROUP_CHAT_MSG == msgtype)
        {
            cout << "群消息[" << js["groupid"]  << "]:" << js["time"].get<string>() << "[" << js["id"] << "] " << js["name"].get<string>() << " " <<
                    "said:" << js["msg"].get<string>() << endl;
            continue;
        }

        if(LOGIN_MSG_ACK == msgtype)
        {
            doLoginResponse(js);//处理登录响应的业务逻辑
            sem_post(&rwsem);//通知主线程，登录结果处理完成
            continue;
        }

        if(REG_MSG_ACK == msgtype)
        {
            doRegResponse(js);
            sem_post(&rwsem);//通知主线程，注册结果处理完成
            continue;
        }
    }
}

//获取系统时间（聊天信息需要添加时间信息）
string getCurremtTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}

//显示登录用户的基本信息
void showCurrentUserData()
{
    cout << "***************login user****************" << endl;
    cout << "current login user -> id:" << g_currentUser.getId() << " " << "name:" << g_currentUser.getName() << endl;
    cout << "---------------friend list-----------------" << endl;
    if(!g_currentUserFriendList.empty())
    {
        for(User& user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }

    cout << "---------------group list-----------------" << endl;
    if(!g_currentUserGroupList.empty())
    {
        for(Group& group : g_currentUserGroupList)
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for(GroupUser& user : group.getUsers())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState() <<
                        " " << user.getRole() << endl;
            }
        }
    }
    cout << "---------------------------------------------" << endl;
}

//"help" command handler
void help(int fd = 0, string = "");
//"chat" command handler
void chat(int, string);
//"addfriend" command handler
void addfriend(int, string);
//"creategroup" command handler
void creategroup(int, string);
//"addgroup" command handler
void addgroup(int, string);
//"groupchat" command handler
void groupchat(int, string);
//"loginout" command handler
void loginout(int, string);

//系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令, 格式help"},
    {"chat", "一对一聊天, 格式chat:friendid:message"},
    {"addfriend", "添加好友, 格式addfriend:friendid"},
    {"creategroup", "创建群组, 格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组, 格式addgroup:groupid"},
    {"groupchat", "群聊, 格式groupchat:groupid:message"},
    {"loginout", "注销, 格式loginout"}
};

//注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout},
};

//主聊天页面程序
void mainMenu(int clientfd)
{
    help();

    char buffer[1024] = {0};
    while(isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command;// 存储命令
        int idx = commandbuf.find(":");
        if(idx == -1)
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0, idx);
        }

        auto it = commandHandlerMap.find(command);
        if(it == commandHandlerMap.end())
        {
            cerr << "invalid input command!"<< endl;
            continue;
        }

        //调用相应命令的事件处理回调，mainMenu对修改封闭，添加新功能不需要球盖该函数
        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx));
    }

}

void help(int, string)
{   
    cout << "show command list" << endl;
    for(auto& p : commandMap)
    {
        cout << p.first << ":" << p.second << endl;
    }
    cout << endl;
}

void addfriend(int clientfd, string str)
{
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer  = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(len == -1)
    {
        cerr << "send addfriend msg error->" << buffer << endl;
    }
}

void chat(int clientfd, string str)
{
    //friendid:message
    int idx = str.find(":");
    if(idx == -1)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friendid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurremtTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(len == -1)
    {
        cerr << "send chat msg error->" << buffer << endl;
    }
}

//"creategroup" command handler
void creategroup(int clientfd, string str)
{
    int idx = str.find(":");
    if(idx == -1)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }

    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(len == -1)
    {
        cerr << "send creategroup msg error ->" << buffer << endl;
    }
}

//"addgroup" command handler
void addgroup(int clientfd, string str)
{
    int groupid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;
    string buffer  = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(len == -1)
    {
        cerr << "send addgroup msg error ->" << buffer << endl;
    }
}

//"groupchat" command handler groupid:message
void groupchat(int clientfd, string str)
{
    int idx = str.find(":");
    if(idx == -1)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    int groupid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurremtTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(len == -1)
    {
        cerr << "send groupchat msg error ->" << buffer << endl;
    }
}

//"loginout" command handler
void loginout(int clientfd, string)
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer  = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if(len == -1)
    {
        cerr << "send loginout msg error->" << buffer << endl;
    }
    else
    {
        isMainMenuRunning = false;
    }
}