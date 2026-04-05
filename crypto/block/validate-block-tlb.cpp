#include <iostream>

#include "td/utils/Timer.h"
#include "vm/boc.h"

#include "block-auto.h"
#include "block-db.h"
#include "block-parse.h"

template <typename F>
void bench(const char* name, int iterations, F&& func) {
  // Warmup
  func();

  td::Timer timer;
  for (int i = 0; i < iterations; i++) {
    if (!func()) {
      std::cerr << name << " failed\n";
      std::_Exit(3);
    }
  }
  double elapsed = timer.elapsed();
  std::cout << name << ": " << iterations << " iters in " << elapsed * 1000.0 << " ms"
            << " (" << elapsed / iterations * 1000.0 << " ms/iter)\n";
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: validate-block-tlb <block.boc>\n";
    return 1;
  }

  auto bytes_r = block::load_binary_file(argv[1]);
  if (bytes_r.is_error()) {
    std::cerr << "failed to read file: " << bytes_r.move_as_error().to_string() << "\n";
    return 2;
  }

  auto boc_r = vm::std_boc_deserialize(bytes_r.move_as_ok());
  if (boc_r.is_error()) {
    std::cerr << "failed to deserialize BOC: " << boc_r.move_as_error().to_string() << "\n";
    return 2;
  }

  auto block_root = boc_r.move_as_ok();

  // Unpack block structure to get sub-cells
  block::gen::Block::Record blk;
  block::gen::BlockExtra::Record extra;
  if (!(tlb::unpack_cell(block_root, blk) && tlb::unpack_cell(blk.extra, extra))) {
    std::cerr << "cannot unpack Block header\n";
    return 2;
  }
  auto inmsg_cs = vm::load_cell_slice_ref(std::move(extra.in_msg_descr));
  auto outmsg_cs = vm::load_cell_slice_ref(std::move(extra.out_msg_descr));
  auto account_blocks = extra.account_blocks;

  constexpr int N = 100;

  // 1. Full block TL-B validation (block::gen::)
  bench("gen::t_Block.validate_ref", N, [&] { return block::gen::t_Block.validate_ref(10000000, block_root); });

  // 2. Handwritten InMsgDescr validation (block::tlb::)
  block::tlb::InMsgDescr t_InMsgDescr{0};
  bench("tlb::InMsgDescr.validate_upto", N, [&] { return t_InMsgDescr.validate_upto(10000000, *inmsg_cs); });

  // 3. Handwritten OutMsgDescr validation (block::tlb::)
  block::tlb::OutMsgDescr t_OutMsgDescr{0};
  bench("tlb::OutMsgDescr.validate_upto", N, [&] { return t_OutMsgDescr.validate_upto(10000000, *outmsg_cs); });

  // 4. Handwritten ShardAccountBlocks validation (block::tlb::)
  bench("tlb::t_ShardAccountBlocks.validate_ref", N,
        [&] { return block::tlb::t_ShardAccountBlocks.validate_ref(10000000, account_blocks); });

  return 0;
}
