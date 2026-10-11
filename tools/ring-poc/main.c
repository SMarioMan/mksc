/* Shared-clock serial ring proof of concept: SO[i] -> SI[i+1], SC common.
 * Start selects the clock owner. Each console discovers its slot in software.
 * A frame token permits each participant to insert one input packet. Nodes
 * forward packets and remove their own after a full lap; the host commits
 * only after every input has returned. No emulator-assigned player IDs. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define REG16(a) (*(volatile u16*)(a))
#define DATA (*(volatile u32*)0x04000120)
#define SIOCNT REG16(0x04000128)
#define RCNT REG16(0x04000134)
#define KEYS REG16(0x04000130)
#define TIMER REG16(0x04000100)
#define TIMER_CNT REG16(0x04000102)
#define TIMER1 REG16(0x04000104)
#define TIMER1_CNT REG16(0x04000106)
enum { DISCOVER = 1, ANNOUNCE, FRAME, INPUT, COMMIT };
enum { NORMAL32 = 0x1000, INTERNAL = 1, START = 0x80 };
struct Status {
    u32 magic, id, count, rounds, errors, timeouts, packets, lastToken;
    u32 inputs[8], frameCycles;
};
#define STATUS (*(volatile struct Status*)0x02000000)
static u32 queue[16];
static u8 head, tail, queued;
static u16 inputs[8];
static u8 seen;
static u32 frameStart;

static u32 clock(void) {
    u16 high, low;
    do {
        high = TIMER1;
        low = TIMER;
    } while (high != TIMER1);
    return (u32)high << 16 | low;
}

static u8 crc(u32 value) {
    u8 c = 0;
    int i, b;
    for (i = 3; i > 0; --i) {
        c ^= value >> (i * 8);
        for (b = 0; b < 8; ++b)
            c = (c & 0x80) ? (c << 1) ^ 7 : c << 1;
    }
    return c;
}
static u32 packet(u32 type, u32 node, u32 sequence, u32 payload) {
    u32 value = type << 28 | (node & 7) << 25 | (sequence & 0x7f) << 18 | (payload & 0x3ff) << 8;
    return value | crc(value);
}
static void push(u32 value) {
    if (queued == 16) { ++STATUS.errors; return; }
    queue[tail] = value;
    tail = (tail + 1) & 15;
    ++queued;
}
static u32 pop(void) {
    if (!queued) return 0;
    u32 value = queue[head];
    head = (head + 1) & 15;
    --queued;
    return value;
}
static void localInput(u32 sequence) {
    u32 id = STATUS.id;
    inputs[id] = ~KEYS & 0x3ff;
    seen |= 1U << id;
    push(packet(INPUT, id, sequence, inputs[id]));
}
static void beginFrame(u32 sequence) {
    seen = 0;
    frameStart = clock();
    push(packet(FRAME, 0, sequence, 0));
    localInput(sequence);
}
int main(void) {
    u32 sequence = 0, shiftsWithoutCommit = 0;
    u16 control = NORMAL32;
    int owner = 0, ownInputReturned = 0, commitPending = 0;
    unsigned i;
    STATUS.magic = 0x52494e47;
    STATUS.id = 0xffffffff;
    STATUS.count = STATUS.rounds = STATUS.errors = 0;
    STATUS.timeouts = STATUS.packets = STATUS.lastToken = STATUS.frameCycles = 0;
    for (i = 0; i < 8; ++i) STATUS.inputs[i] = 0;
    REG16(0x04000000) = 0x403;
    volatile u16* screen = (volatile u16*)0x06000000;
    for (i = 0; i < 240 * 160; ++i) screen[i] = 0x4210;
    TIMER_CNT = 0x80;
    TIMER1_CNT = 0x84;
    RCNT = 0;
    SIOCNT = control;
    for (;;) {
        if (!owner && STATUS.id == 0xffffffff && !(KEYS & 8)) {
            owner = 1;
            control |= INTERNAL;
            STATUS.id = 0;
            push(packet(DISCOVER, 0, 0, 1));
        }
        if (owner) {
            /* 244us software turnaround plus 128us wire time at 256kHz. */
            u16 then = TIMER;
            while ((u16)(TIMER - then) < 4096) { }
        }
        SIOCNT = control;
        DATA = pop();
        SIOCNT = control | START;
        u32 spins = 0;
        while (SIOCNT & START) {
            if (!owner && STATUS.id == 0xffffffff && !(KEYS & 8)) break;
            if (++spins == 1000000) { ++STATUS.timeouts; break; }
        }
        if (SIOCNT & START) { SIOCNT = control; continue; }
        u32 received = DATA;
        ++STATUS.packets;
        if (owner && ++shiftsWithoutCommit > 128) {
            /* Lost/invalid data must not leave a session permanently idle.
             * Re-enumerate after a bounded wait. A broken wire still fails,
             * but reconnecting permits a fresh session without a reset. */
            ++STATUS.timeouts;
            head = tail = queued = seen = 0;
            sequence = STATUS.count = 0;
            ownInputReturned = commitPending = 0;
            shiftsWithoutCommit = 0;
            push(packet(DISCOVER, 0, 0, 1));
            continue;
        }
        if (!received) continue;
        if ((u8)received != crc(received)) { ++STATUS.errors; continue; }
        u32 type = received >> 28;
        u32 node = (received >> 25) & 7;
        u32 seq = (received >> 18) & 0x7f;
        u32 payload = (received >> 8) & 0x3ff;
        if (type == DISCOVER) {
            if (owner) {
                if (payload < 2 || payload > 8) { ++STATUS.errors; continue; }
                STATUS.count = payload;
                push(packet(ANNOUNCE, 0, 0, payload));
            } else {
                if (payload >= 8) { ++STATUS.errors; continue; }
                head = tail = queued = seen = 0;
                sequence = STATUS.count = 0;
                STATUS.id = payload;
                push(packet(DISCOVER, 0, 0, payload + 1));
            }
        } else if (type == ANNOUNCE) {
            if (payload < 2 || payload > 8 || STATUS.id >= payload) { ++STATUS.errors; continue; }
            STATUS.count = payload;
            screen[STATUS.id] = 0x03e0;
            if (owner) beginFrame(sequence);
            else push(received);
        } else if (type == FRAME && STATUS.count) {
            if (seq != sequence) ++STATUS.errors;
            if (!owner) {
                seen = 0;
                push(received);
                localInput(sequence);
            }
        } else if (type == INPUT && STATUS.count) {
            if (seq != sequence || node >= STATUS.count) { ++STATUS.errors; continue; }
            if (node == STATUS.id) {
                if (inputs[node] != payload) ++STATUS.errors;
                if (owner) ownInputReturned = 1;
            } else {
                if (seen & (1U << node)) ++STATUS.errors;
                inputs[node] = payload;
                seen |= 1U << node;
                push(received);
            }
        } else if (type == COMMIT && STATUS.count) {
            if (seq != sequence || seen != ((1U << STATUS.count) - 1)) ++STATUS.errors;
            for (i = 0; i < STATUS.count; ++i) STATUS.inputs[i] = inputs[i];
            ++STATUS.rounds;
            STATUS.lastToken = received;
            screen[240 + STATUS.id] = STATUS.errors ? 0x001f : 0x7c00;
            ++sequence;
            sequence &= 0x7f;
            if (owner) {
                STATUS.frameCycles = clock() - frameStart;
                shiftsWithoutCommit = 0;
                ownInputReturned = commitPending = 0;
                beginFrame(sequence);
            } else push(received);
        } else ++STATUS.errors;
        if (owner && ownInputReturned && !commitPending && seen == ((1U << STATUS.count) - 1)) {
            push(packet(COMMIT, 0, sequence, 0));
            commitPending = 1;
        }
    }
}
