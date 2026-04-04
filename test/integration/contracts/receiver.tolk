import "@stdlib/gas-payments"

struct ReceiverStorage {
    seqno: uint32
    id: uint64
}

struct BurnGasSig {
    sigHi: uint256
    sigLo: uint256
}

struct (0xDEADBEEF) BurnGasMsg {
    hash: uint256
    pubkey: uint256
    iterations: uint32
    sig: Cell<BurnGasSig>
}

fun onExternalMessage(inMsg: slice) {
    acceptExternalMessage();
    var st = ReceiverStorage.fromCell(contract.getData());
    st.seqno += 1;
    contract.setData(st.toCell());

    val nonce = inMsg.loadUint(32);
    val iterations = inMsg.loadUint(32);

    val msg = createMessage({
        bounce: BounceMode.NoBounce,
        value: ton("10"),
        dest: contract.getAddress(),
        body: BurnGasMsg {
            hash: 0x2CF24DBA5FB0A30E26E83B2AC5B9E29E1B161E5C1FA7425E73043362938B9824,
            pubkey: 0x193F88C0D2C935C35132FC95AF3B39CC6E6320898EDB314E0C376C2E37F29267,
            iterations: iterations,
            sig: BurnGasSig {
                sigHi: 0xB1BBC677ADBA089113AF5A63C2DA2C72A5FDB3213E0827515D0BD7C2FCAA6C9E,
                sigLo: 0xD42A382408302CB466495598041B88808EEBD0AE242C8068731A68BC508FEA0A,
            }.toCell(),
        },
    });
    msg.send(SEND_MODE_REGULAR);
}

fun onInternalMessage(in: InMessage) {
    if (in.body.remainingBitsCount() < 32) {
        return;
    }
    val m = BurnGasMsg.fromSlice(in.body);
    val s = m.sig.load();
    val sig = beginCell()
        .storeUint(s.sigHi, 256)
        .storeUint(s.sigLo, 256)
        .endCell().beginParse();
    var count = 0;
    repeat (m.iterations) {
        if (isSignatureValid(m.hash, sig, m.pubkey)) {
            count += 1;
        }
    }
    // Store count to force the loop to actually execute
    contract.setData(beginCell().storeUint(count, 32).storeUint(m.hash, 64).endCell());
}
