#include "../api/sse_stream.h"

#include <cassert>
#include <iostream>
#include <thread>

int main()
{
    SseStream stream(16);
    assert(stream.push("first"));
    assert(stream.queued_bytes() == 5);
    std::string chunk;
    assert(stream.try_pop(chunk));
    assert(chunk == "first");
    assert(!stream.try_pop(chunk));

    stream.push("done");
    stream.finish();
    assert(!stream.finished_and_empty());
    assert(stream.try_pop(chunk));
    assert(stream.finished_and_empty());

    SseStream canceled;
    canceled.cancel();
    assert(canceled.canceled());
    assert(!canceled.push("ignored"));

    std::cout << "sse_stream_test: all checks passed\n";
    return 0;
}
