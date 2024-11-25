#include "json.hpp"
using json = nlohmann::json;

#include <iostream>
#include <vector>
#include <map>
#include <string>
using namespace std;

//json序列化示例1
string func1(){
    json js;
    js["msg_type"] = 2;
    js["from"] = "zhang san";
    js["to"] = "li si";
    js["msg"] = "hello world";

    string sendBuf = js.dump();
    //cout << js << endl;
    //cout << sendBuf.c_str() << endl;
    return sendBuf;
}

string func2(){
    json js;
    //添加数组
    js["id"] = {1, 2, 3, 4, 5};
    //添加key-value
    js["name"] = "zhang san";
    //添加对象
    js["msg"]["zhang san"] = "hello world";
    js["msg"]["li si"] = "hello china";
    //上面等同于下面这句一次性添加数组对象
    js["msg"] = {{"zhang san", "hello world"}, {"li si", "hello china"}};
    //cout << js << endl;
    string sendBuf = js.dump();

    return sendBuf;
}

string func3(){
    json js;

    vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    js["list"] = v;

    map<int, string> m;
    m.insert({1, "黄山"});
    m.insert({2, "华山"});
    m.insert({3, "嵩山"});
    js["path"] = m;

    string sendBuf = js.dump();//json数据对象 序列化成json字符串
    //cout << sendBuf.c_str() << endl;
    return sendBuf;
}

int main()
{
    //func1();
    //func2();
    //func3();

    // string recvBuf = func1();
    // //数据的反序列化 json字符串 -> json数据对象
    // json jsbuf = json::parse(recvBuf);
    // cout << jsbuf["msg_type"] << endl;
    // cout << jsbuf["from"] << endl;
    // cout << jsbuf["to"] << endl;
    // cout << jsbuf["msg"] << endl;

    // string recvBuf = func2();
    // json jsBuf = json::parse(recvBuf);
    // cout << jsBuf["id"] << endl;
    // auto arr = jsBuf["id"];
    // cout << arr[2] << endl;

    // auto msgjs = jsBuf["msg"];
    // cout << msgjs["zhang san"] << endl;
    // cout << msgjs["li si"] << endl;

    string recvBuf = func3();
    json jsBuf = json::parse(recvBuf);

    vector<int> vec = jsBuf["list"];
    map<int, string> mp = jsBuf["path"];

    for(int v : vec){
        cout << v << " ";
    }

    cout << endl;

    for(auto p : mp){
        cout << p.first << " " << p.second << " ";
    }
    cout << endl;
    return 0;
}