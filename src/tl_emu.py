import chiavdf

import asyncio, random, sys

# Overview of Timelord <-> VDF client protocol

# Timelord -> VDF client
# Session init: "T", "S" or "N" (prover type)
# Initial value: "<discr size>" "<discr>" "<form size>" "<compressed form>"
# ("<discr size>" in decimal (3 digits), "<discr>" parsable by GMP)
# "<form size>" as 1 byte, "<compressed form>" as is

# VDF client -> Timelord
# "OK"

# Timelord -> VDF client
# Iters: iters size "NN" (decimal) and iter count "NNNN..." (decimal)
# "010" to stop

# VDF client -> Timelord (if proof requested)
# "<proof size>" (4 bytes, big endian)
# "<proof>" (hex encoded)
# Proof items (bytes): iters (8), y size (8), y, witness type (1), proof

# VDF client -> Timelord
# "STOP"

# Timelord -> VDF client
# "ACK"

top_seed = 0xae0666f161fed1a
DISCR_BITS = 1024
MAX_CONNS = 3
INIT_FORM = b"\x08" + b"\x00" * 99
MIN_ITERS = 500 * 1000
MAX_ITERS = 10 * 1000**2
MIN_WAIT = 5
MAX_WAIT = 100
N_PROOFS = 4

def get_discr(conn_idx, cnt):
    s = (top_seed << 16) + (conn_idx << 14) + cnt
    return chiavdf.create_discriminant(s.to_bytes(16, 'big'), DISCR_BITS)

conn_idxs = set()
cnts = [0 for i in range(MAX_CONNS)]

def get_conn_idx():
    for i in range(MAX_CONNS):
        if i not in conn_idxs:
            conn_idxs.add(i)
            return i
    raise ValueError("Too many connections!")

def clear_conn_idx(idx):
    conn_idxs.remove(idx)

async def send_msg(w, msg):
    #print("Sending:", msg)
    w.write(msg)
    await w.drain()

def decode_resp(data):
    return data.decode(errors="replace")

async def read_conn(reader, writer, idx, task):
    data = await reader.read(4)
    if data == b"STOP":
        await send_msg(writer, b"ACK")
        print("Closing connection for VDF %d" % (idx,))
        writer.close()
        task.cancel()
    else:
        size = int.from_bytes(data, 'big')
        data = await reader.readexactly(size)
        print("Got proof!")

async def init_conn(reader, writer, idx):
    d = get_discr(idx, cnts[idx]).encode()

    await send_msg(writer, b"N")
    msg = b"%03d%s" % (len(d), d)
    await send_msg(writer, msg)
    msg = b"%c%s" % (len(INIT_FORM), INIT_FORM)
    await send_msg(writer, msg)
    print("Sent initial value for VDF %d, cnt %d" % (idx, cnts[idx]))
    print(" d = %s" % (d.decode(),))

    ok = decode_resp(await reader.read(2))
    if ok != "OK":
        raise ValueError("Bad response from VDF client: %s" % (ok,))

    task = asyncio.current_task()
    read_task = asyncio.create_task(read_conn(reader, writer, idx, task))
    wait_sec = random.randint(MIN_WAIT, MAX_WAIT)
    print("Waiting %d sec for VDF %d, cnt %d" % (wait_sec, idx, cnts[idx]))
    cnts[idx] += 1
    try:
        await asyncio.sleep(1)
        iters_list = [random.randint(MIN_ITERS, MAX_ITERS) for _ in range(N_PROOFS)]
        iters_enc = "".join("%02d%d" % (len(str(n)), n) for n in iters_list)
        print("Requesting proofs for iters:", iters_list)
        await send_msg(writer, iters_enc.encode())

        await asyncio.sleep(wait_sec)

        await send_msg(writer, b"010")
        print("Stopping VDF", idx)
    except asyncio.CancelledError:
        print("VDF %d task cancelled" % (idx,))

    await read_task

async def conn_wrapper(r, w):
    idx = get_conn_idx()
    try:
        await init_conn(r, w, idx)
    except Exception as e:
        print(e)
    clear_conn_idx(idx)

async def main():
    seed = 1
    if len(sys.argv) > 1:
        seed = int(sys.argv[1])
    random.seed(seed)
    server = await asyncio.start_server(conn_wrapper, '127.0.0.1', 8000)

    addrs = ', '.join(str(sock.getsockname()) for sock in server.sockets)
    print(f'Serving on {addrs}')

    await server.serve_forever()

try:
    asyncio.run(main())
except KeyboardInterrupt:
    print("Stopped")
