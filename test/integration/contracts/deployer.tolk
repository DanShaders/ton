import "@stdlib/gas-payments"
import "@stdlib/tvm-dicts"

fun onExternalMessage(inMsg: slice) {
    acceptExternalMessage();

    var ds = contract.getData().beginParse();
    var deployed = ds.loadDict();
    val childCode = ds.loadRef();

    val startIdx = inMsg.loadUint(64);
    val batchSize = inMsg.loadUint(32);

    // Check if this batch was already deployed
    val (_, found) = deployed.uDictGet(64, startIdx);
    if (found) {
        return;
    }

    var i: int = 0;
    while (i < batchSize) {
        val idx = startIdx + i;
        val childData = beginCell()
            .storeUint(0, 32)
            .storeUint(idx, 64)
            .endCell();

        val msg = createMessage({
            bounce: BounceMode.NoBounce,
            value: ton("100"),
            dest: AutoDeployAddress {
                stateInit: { code: childCode, data: childData },
            },
        });
        msg.send(SEND_MODE_REGULAR);
        i += 1;
    }

    // Mark this batch as deployed
    deployed.uDictSetBuilder(64, startIdx, beginCell().storeUint(1, 1));
    contract.setData(
        beginCell()
            .storeDict(deployed)
            .storeRef(childCode)
            .endCell()
    );
}

fun onInternalMessage(in: InMessage) {
}
