#include "chatservice.h"
#include "public.h"
#include <muduo/base/Logging.h>
#include <vector>
#include <map>
using namespace std;
using namespace muduo;

ChatService* ChatService::instance()
{
    static ChatService service;
    return &service;
}

//注册消息以及对应的handler回调操作
ChatService::ChatService()
{
    //用户基本业务管理相关事件处理回调注册
    msgHandlerMap_.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    msgHandlerMap_.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    msgHandlerMap_.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    msgHandlerMap_.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    msgHandlerMap_.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    //群组业务管理相关事件处理回调注册
    msgHandlerMap_.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    msgHandlerMap_.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    msgHandlerMap_.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    //连接redis服务器
    if(redis_.connect())
    {
        //设置上报消息的回调
        redis_.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }

}

//获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    //记录错误日志，msgid没有对应的事件处理回调
    auto it = msgHandlerMap_.find(msgid);
    if(it == msgHandlerMap_.end())
    {
        //返回一个默认的处理器 空操作
        return [=](const TcpConnectionPtr& conn, json& js, Timestamp){
            LOG_ERROR << "msgid:" << msgid << "can not find handler!";
        };
    }
    else
    {
        return msgHandlerMap_[msgid];
    }
}

//处理登录业务
void ChatService::login(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = userModel_.query(id);
    if(user.getId() == id && user.getPwd() == pwd)
    {
        if(user.getState() == "online")
        {
            //该用户已经登录，不允许重复登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this account is using, input another!";
            conn->send(response.dump());
        }
        else
        {
            //登录成功记录 用户连接信息
            {
                lock_guard<mutex> lock(connMutex_);
                userConnMap_.insert({id, conn});
            }

            //id用户登陆成功之后 向redis订阅chanel（id）
            redis_.subscribe(id);

            //登录成功，更新用户状态信息
            user.setState("online");
            userModel_.updateState(user);

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId(); 
            response["name"] = user.getName();

            //查询用户是否有离线消息
            vector<string> vec = offlineMsgModel_.query(id);
            if(!vec.empty())
            {
                response["offlinemsg"] = vec;
                //读取完 把离线消息删除
                offlineMsgModel_.remove(id);
            }

            //查询该用户的好友信息并返回
            vector<User> userVec = friendModel_.query(id);
            if(!userVec.empty())
            {
                vector<string> vec2;
                for(User& user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();

                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            //查询用户的群组信息
            vector<Group> groupuserVec = groupmodel_.queryGroups(id);
            if(!groupuserVec.empty())
            {
                vector<string> groupV;
                for(Group& group : groupuserVec)
                {
                    json grpjson;
                    grpjson["id"] = group.getId();
                    grpjson["groupname"] = group.getName();
                    grpjson["groupdesc"] = group.getDesc();

                    vector<string> userV;
                    for(GroupUser& user : group.getUsers())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    grpjson["users"] = userV;
                    groupV.push_back(grpjson.dump());
                }
                response["groups"] = groupV;
            }

            conn->send(response.dump());
        }
    }
    else
    {
        //用户不存在 用户存在但密码错误
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "name or password is invalid!";
        conn->send(response.dump());
    }
}

//处理注册业务 name password
void ChatService::reg(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = userModel_.insert(user);
    if(state)
    {
        //注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId(); 
        conn->send(response.dump());
    }
    else
    {
        //注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        response["id"] = user.getId(); 
        conn->send(response.dump());
    }
}

//处理注销业务
void ChatService::loginout(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int userid = js["id"].get<int>();

    {
        lock_guard<mutex> lock(connMutex_);
        auto it = userConnMap_.find(userid);
        if(it != userConnMap_.end())
        {
            userConnMap_.erase(it);
        }
    }

    //在redis中取消订阅通道
    redis_.unsubscribe(userid);

    //更新用户状态信息
    User user(userid, "", "", "offline");
    userModel_.updateState(user);
}

//处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr& conn)
{
    User user;
    {
        lock_guard<mutex> lock(connMutex_);
        for(auto it = userConnMap_.begin(); it != userConnMap_.end(); ++ it)
        {
            if(it->second == conn)
            {
                user.setId(it->first);
                //从map表删除用户连接信息
                userConnMap_.erase(it);
                break;
            }
        }
    }

    //取消redis订阅通道
    redis_.unsubscribe(user.getId());

    //更新用户状态信息
    if(user.getId() != -1)
    {
        user.setState("offline");
        userModel_.updateState(user);
    }
}

//服务器异常，业务重置方法
void ChatService::reset()
{
    //把online用户重置offline
    userModel_.resetState();
}
 
//一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int toid = js["toid"].get<int>();

    {
        lock_guard<mutex> lock(connMutex_);
        auto it = userConnMap_.find(toid);
        if(it != userConnMap_.end())
        {
            //在线，转发消息 服务器主动推送消息给toid用户
            it->second->send(js.dump());
            return;
        }
    }

    //查询toid是否在线
    User user = userModel_.query(toid);
    if(user.getState() == "online")
    {
        redis_.publish(toid, js.dump());
        return;
    }

    //不在线，存储离线消息
    offlineMsgModel_.insert(toid, js.dump());

}

//添加好友
void ChatService::addFriend(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    friendModel_.insert(userid, friendid);
}

 //创建群组业务
void ChatService::createGroup(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    //存储新创建的群组信息
    Group group(-1, name, desc);

    if(groupmodel_.createGroup(group))
    {
        //存储创建群组人的信息
        groupmodel_.addGroup(userid, group.getId(), "creator");
    }
}

//加入群组业务
void ChatService::addGroup(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();

    groupmodel_.addGroup(userid, groupid, "normal");
}

//群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr& conn, json& js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = groupmodel_.queryGroupUsers(userid, groupid);
    lock_guard<mutex> lock(connMutex_);
    for(int id : useridVec)
    {
        auto it = userConnMap_.find(id);
        if(it != userConnMap_.end())
        {
            //转发群消息
            it->second->send(js.dump());
        }
        else
        {
            //查询toid是否在线
            User user = userModel_.query(id);
            if(user.getState() == "online")
            {
                redis_.publish(id, js.dump());
            }
            else
            {
                //存储离线群组消息
                offlineMsgModel_.insert(id, js.dump());
            }
        }
    }
}

void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(connMutex_);
    auto it = userConnMap_.find(userid);
    if(it != userConnMap_.end())
    {
        it->second->send(msg);
        return;
    }
    //存储该用户的离线消息
    offlineMsgModel_.insert(userid, msg);
}