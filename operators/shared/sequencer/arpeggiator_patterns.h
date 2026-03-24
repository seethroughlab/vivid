#pragma once

namespace vivid_sequencers {

inline int arp_pattern_index(int mode, int raw_step, int pool_count) {
    if (pool_count <= 0) return 0;

    switch (mode) {
        case 0: // Up
            return raw_step % pool_count;

        case 1: // Down
            return (pool_count - 1) - (raw_step % pool_count);

        case 2: { // UpDown (exclusive endpoints)
            if (pool_count == 1) return 0;
            int cycle = (pool_count - 1) * 2;
            int pos = raw_step % cycle;
            return (pos < pool_count) ? pos : (cycle - pos);
        }

        case 3: { // DownUp (exclusive endpoints)
            if (pool_count == 1) return 0;
            int cycle = (pool_count - 1) * 2;
            int pos = raw_step % cycle;
            int down_idx = (pool_count - 1) - pos;
            int up_idx = pos - (pool_count - 1);
            return (pos < pool_count) ? down_idx : up_idx;
        }

        case 5: // Order (input order)
            return raw_step % pool_count;

        case 6: { // Converge: alternate lowest/highest moving inward
            if (pool_count == 1) return 0;
            int cycle_pos = raw_step % pool_count;
            int pair = cycle_pos / 2;
            bool is_high = (cycle_pos % 2) != 0;
            return is_high ? ((pool_count - 1) - pair) : pair;
        }

        case 7: { // Diverge: start from middle, alternate outward
            if (pool_count == 1) return 0;
            int mid = pool_count / 2;
            int cycle_pos = raw_step % pool_count;
            int offset = (cycle_pos + 1) / 2;
            bool go_down = (cycle_pos % 2) == 0;
            int idx = go_down ? (mid - offset) : (mid + offset);
            if (idx < 0) idx = 0;
            if (idx >= pool_count) idx = pool_count - 1;
            return idx;
        }

        case 9: // OrderDown (reverse input order)
            return (pool_count - 1) - (raw_step % pool_count);

        default:
            return raw_step % pool_count;
    }
}

} // namespace vivid_sequencers
