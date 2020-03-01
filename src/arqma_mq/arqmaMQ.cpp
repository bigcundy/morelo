#include "arqmaMQ.h"


namespace arqmaMQ 
{
    extern "C" void message_buffer_destroy(void*, void* hint) {
        delete reinterpret_cast<std::string*>(hint);
    }

    inline static int
    s_send(void *socket, const char *string, int flags = 0) {
        int rc;
        zmq_msg_t message;
        zmq_msg_init_size(&message, strlen(string));
        memcpy(zmq_msg_data(&message), string, strlen(string));
        rc = zmq_msg_send(&message, socket, flags);
        assert(-1 != rc);
        zmq_msg_close(&message);
        return (rc);
    }

    inline static bool
    s_send (zmq::socket_t & socket, const std::string & string, int flags = 0) {

        zmq::message_t message(string.size());
        memcpy (message.data(), string.data(), string.size());
        bool rc = socket.send (message, flags);
        return (rc);
    }

    inline static bool 
    s_sendmore (zmq::socket_t & socket, const std::string & string) {
        zmq::message_t message(string.size());
        memcpy (message.data(), string.data(), string.size());
        bool rc = socket.send (message, ZMQ_SNDMORE);
        return (rc);
    }


    ArqmaNotifier::ArqmaNotifier()
    {
        producer.bind("inproc://backend");
        proxy_thread = std::thread{&ArqmaNotifier::proxy_loop, this};
    }

    ArqmaNotifier::~ArqmaNotifier()
    {
        producer.send(create_message(std::move("QUIT")), 0);
        proxy_thread.join();
        zmq_close(&producer);
        zmq_close(&subscriber);
        zmq_term(&context);
    }

    zmq::message_t ArqmaNotifier::create_message(std::string &&data)
    {
        auto *buffer = new std::string(std::move(data));
        return zmq::message_t{&(*buffer)[0], buffer->size(), message_buffer_destroy, buffer};
    };

    void ArqmaNotifier::notify(const cryptonote::block bl)
    {
        //std::cout << data << std::endl;
	rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
	writer.StartObject();
	writer.Key("result");
	writer.Key("top_block_hash");
	writer.String("stuff");
//        writer.Key("height");
//        writer.Uint64(bl.height);
	writer.EndObject();

        producer.send(create_message(std::move("getblocktemplate")), ZMQ_SNDMORE);
        producer.send(create_message(std::move(sb.GetString())), 0);
    }


    void ArqmaNotifier::notify(std::string &&data)
    {
        //std::cout << data << std::endl;
        producer.send(create_message(std::move("getblocktemplate")), ZMQ_SNDMORE);
        producer.send(create_message(std::move(data)), 0);
    }

    void ArqmaNotifier::proxy_loop()
    {
        subscriber.connect("inproc://backend");
        listener.bind("tcp://*:3000");

        zmq::pollitem_t items[2];
        items[0].socket = (void*)subscriber;
        items[0].fd = 0;
        items[0].events = ZMQ_POLLIN;
        items[1].socket = (void*)listener;
        items[1].fd = 0;
        items[1].events = ZMQ_POLLIN;

        std::string id;
        std::string quit("QUIT");

	rapidjson::StringBuffer sb;
	rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

        while (true) 
        {
            int rc = zmq::poll(items, 2, 1);
            if (items[0].revents & ZMQ_POLLIN)
            {
                zmq::message_t envelope;
                subscriber.recv(&envelope);
                std::string stop = std::string(static_cast<char*>(envelope.data()), envelope.size());
                if (stop == quit) 
                {
                    std::cout << "closing thread" << std::endl;
                    break;
                }
                subscriber.recv(&envelope);
                std::string identity = std::string(static_cast<char*>(envelope.data()), envelope.size());
                std::cout << identity << std::endl;

                //TODO: iterate list of <id, command>
                if (!id.empty())
                {

//		    rapidjson::StringBuffer sb;
//                    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
//		    writer.StartObject();
//			writer.Key("result");
//			writer.Key("top_block_hash");
//			writer.String(identity.c_str());
//		    writer.EndObject();

                  s_sendmore(listener, id);
                    s_send(listener, identity);
                }

            }

            if (items[1].revents & ZMQ_POLLIN)
            {
                zmq::message_t envelope1;
                listener.recv(&envelope1);
                std::string msg = std::string(static_cast<char*>(envelope1.data()), envelope1.size());
                //TODO: record <id, command>
                id = std::move(msg);
                listener.recv(&envelope1);
                listener.recv(&envelope1);
                std::string msg1 = std::string(static_cast<char*>(envelope1.data()), envelope1.size());
                std::cout << "received " <<  id << " " << msg1 << std::endl;
            }
        }
        zmq_close(&listener);
    }
}
