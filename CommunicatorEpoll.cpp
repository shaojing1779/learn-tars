#include "CommunicatorEpoll.h"
#include "ObjectProxy.h"

using namespace std;

namespace tars
{
CommunicatorEpoll::CommunicatorEpoll()
{
    _ep.create(1024);

    _shutdown.createSocket();
    _ep.add(_shutdown.getfd(), 0, EPOLLIN);

    //初始化请求的事件通知
    for(size_t i = 0; i < 2048; ++i)
    {
        _notify[i].bValid = false;
    }
}

CommunicatorEpoll::~CommunicatorEpoll()
{
}

void CommunicatorEpoll::addFd(int fd, FDInfo * info, uint32_t events)
{
    _ep.add(fd,(uint64_t)info,events);
}

void CommunicatorEpoll::delFd(int fd, FDInfo * info, uint32_t events)
{
    _ep.del(fd,(uint64_t)info,events);
}

void CommunicatorEpoll::notify(size_t iSeq,ReqInfoQueue * msgQueue)
{
    if(_notify[iSeq].bValid)
    {
        _ep.mod(_notify[iSeq].notify.getfd(),(long long)&_notify[iSeq].stFDInfo, EPOLLIN);
        assert(_notify[iSeq].stFDInfo.p == (void*)msgQueue);
    }
    else
    {
        _notify[iSeq].stFDInfo.iType   = FDInfo::ET_C_NOTIFY;
        _notify[iSeq].stFDInfo.p       = (void*)msgQueue;
        _notify[iSeq].stFDInfo.fd      = _notify[iSeq].eventFd;
        _notify[iSeq].stFDInfo.iSeq    = iSeq;
        _notify[iSeq].notify.createSocket();
        _notify[iSeq].bValid           = true;

        _ep.add(_notify[iSeq].notify.getfd(),(long long)&_notify[iSeq].stFDInfo, EPOLLIN);
    }
}

void CommunicatorEpoll::run()
{
    while (true)
    {
        try
        {
            int num = _ep.wait(100);

            //先处理epoll的网络事件
            for (int i = 0; i < num; ++i)
            {
                const epoll_event& ev = _ep.get(i);
                uint64_t data = ev.data.u64;

                if(data == 0)
                {
                    continue; //data非指针, 退出循环
                }

                handle((FDInfo*)data, ev.events);
            }
        }
        catch (exception& e)
        {
        }
        catch (...)
        {
        }
    }
}


void CommunicatorEpoll::handle(FDInfo * pFDInfo, uint32_t events)
{
    try
    {
        assert(pFDInfo != NULL);

        //队列有消息通知过来
        if(FDInfo::ET_C_NOTIFY == pFDInfo->iType)
        {
            ReqInfoQueue * pInfoQueue=(ReqInfoQueue*)pFDInfo->p;
            ReqMessage * msg = NULL;

            try
            {
                while(pInfoQueue->pop_front(msg))
                {
                    //线程退出了
                    if(ReqMessage::THREAD_EXIT == msg->eType)
                    {
                        assert(pInfoQueue->empty());

                        delete msg;
                        msg = NULL;

                        _ep.del(_notify[pFDInfo->iSeq].notify.getfd(),(long long)&_notify[pFDInfo->iSeq].stFDInfo, EPOLLIN);

                        delete pInfoQueue;
                        pInfoQueue = NULL;

                        _notify[pFDInfo->iSeq].stFDInfo.p = NULL;
                        _notify[pFDInfo->iSeq].notify.close();
                        _notify[pFDInfo->iSeq].bValid = false;

                        return;
                    }

                    msg->pObjectProxy->invoke(msg);
                }
            }
            catch(exception & e)
            {
            }
            catch(...)
            {
            }
        }
        else
        {
			Transceiver *pTransceiver = (Transceiver*)pFDInfo->p;
            //先收包
            if (events & EPOLLIN)
            {
            	handleInputImp(pTransceiver);
            }

            //发包
            if (events & EPOLLOUT)
            {
                handleOutputImp(pTransceiver);
            }

            //连接出错 直接关闭连接
            if(events & EPOLLERR)
            {
            }
        }
    }
    catch(exception & e)
    {
    }
    catch(...)
    {
    }
}


void CommunicatorEpoll::handleInputImp(Transceiver * pTransceiver)
{
	list<string> done;	
	if(pTransceiver->doResponse(done) > 0)
    {	
        list<string>::iterator it = done.begin();
        for (; it != done.end(); ++it)
        {
			string response = *it;
			pTransceiver->getObjProxy()->finishInvoke(response);	
				
        }
	}
}


void CommunicatorEpoll::handleOutputImp(Transceiver * pTransceiver)
{
	pTransceiver->doRequest();
}


}

