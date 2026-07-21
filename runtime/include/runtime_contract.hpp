#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <set>
#include <tuple>
#include <vector>

namespace runtime_contract
{
    // Supported Shipping build: FProperty ArrayDim@0x30,
    // ElementSize@0x34, PropertyFlags@0x38.
    constexpr std::size_t FPropertyElementSizeOffset = 0x34;
    constexpr int InternalNoResendMaxCallsPerTick = 6;
    constexpr int DirectLocalImmediateRepostsPerDeferredWakeup = 1;
    constexpr int MaximumNetworkBatchLimit = 500;
    constexpr int FastLocalCadenceMs = 17;
    constexpr int IncrementalTextureImportPacingMs = 100;
    constexpr int IncrementalTextureImportMinimumStrokes = 40;
    constexpr int FallbackOutgoingStrokesPerBatch = 20;
    constexpr int FallbackOutgoingBatchesPerSecond = 20;
    constexpr int FallbackReplicatedStrokesPerTick = 24;
    constexpr int FallbackRenderTargetWritesPerFrame = 6;
    constexpr int MinimumNetworkPacingMs = 1;
    constexpr int MaximumManualNetworkPacingMs = 500;
    constexpr int ServerPackedFallbackBatchLimit = 20;
    constexpr int ServerPackedFallbackPacingMs = 50;
    constexpr std::uint64_t LocalDispatchCpuBudgetUs = 4'000;

    // MECCHA CHAMELEON 2.9.0 packed paint wire contract. Keep these values
    // centralized so the encoder and native regression test cannot silently
    // disagree about the format-2 layout.
    constexpr std::uint8_t PackedPaintFormatVersion = 2;
    constexpr std::size_t PackedPaintHeaderBytes = 21; // version + FGuid + count
    constexpr std::size_t PackedPaintRecordBytes = 31;
    constexpr std::size_t PackedPaintRecordAlbedoOffset = 12;
    constexpr std::size_t PackedPaintRecordMetallicOffset = 16;
    constexpr std::size_t PackedPaintRecordRoughnessOffset = 17;
    constexpr std::size_t PackedPaintRecordEmissiveOffset = 18;
    constexpr std::size_t PackedPaintRecordChannelOffset = 22;
    constexpr std::size_t PackedPaintRecordWorldRadiusOffset = 23;
    constexpr std::size_t PackedPaintRecordSubdivisionOffset = 27;

    // UE 5.6 packs Metallic, Roughness, and Emissive into one material-properties
    // render target (R/G/B).  Channel 7 updates that target atomically.  Splitting
    // a sample into channels 5 and 6 doubles the packed work and allowed the
    // separate local replication route to render a second visible pass.
    constexpr std::array<std::uint8_t, 1> ProductionMaterialPaintChannels{7};

    constexpr std::size_t packed_paint_payload_size(std::size_t stroke_count)
    {
        return PackedPaintHeaderBytes + stroke_count * PackedPaintRecordBytes;
    }

    constexpr std::size_t production_material_stroke_count(std::size_t sample_count)
    {
        return sample_count * ProductionMaterialPaintChannels.size();
    }

    constexpr std::size_t production_material_sample_index(std::size_t stroke_index)
    {
        return stroke_index / ProductionMaterialPaintChannels.size();
    }

    static_assert(PackedPaintRecordEmissiveOffset + 4 == PackedPaintRecordChannelOffset,
                  "packed paint emissive layout mismatch");
    static_assert(PackedPaintRecordWorldRadiusOffset + sizeof(float) ==
                      PackedPaintRecordSubdivisionOffset,
                  "packed paint world-radius layout mismatch");
    static_assert(PackedPaintRecordSubdivisionOffset + 4 == PackedPaintRecordBytes,
                  "packed paint record size mismatch");

    // Packed skeletal strokes carry both a UV-space brush radius and an optional
    // world-space radius.  In the supported Shipping build, compact-stroke
    // expansion at RVA 0x50F65A0 calls the skeletal preflight at RVA 0x50F6110.
    // A non-positive world radius is the sentinel that asks that preflight to
    // derive the world radius from the mesh bounds and UV radius.  Supplying the
    // normalized UV radius here is not equivalent: it suppresses that conversion
    // and collapses otherwise large brushes to tiny world-space dots.
    constexpr float PackedMeshAnchorWorldRadiusAuto = 0.0f;
    // Production preserves the configured UV radius on the wire. Each anchor's
    // effective world radius is derived independently from that triangle's
    // UV-to-world Jacobian; a uniform wire multiplier either leaves holes or
    // expands one world-space stroke across adjacent mesh surfaces.
    constexpr double PackedMeshAnchorProductionRadiusScale = 1.0;
    constexpr bool PackedMeshAnchorProductionUsesTriangleWorldRadius = true;
    // Research-only dynamic calibration retains the conservative fold/seam
    // factor for controlled comparisons.
    constexpr double PackedMeshAnchorCoverageSafetyFactor = 0.91;
    constexpr int PackedMeshAnchorSubdivisionLevelAuto = 0;
    constexpr float PackedMeshAnchorSubdivisionPixelSizeAuto = 0.0f;
    constexpr int PackedMeshAnchorTemplateResolutionAuto = 0;

    constexpr bool packed_mesh_anchor_requests_world_radius_conversion(float effective_world_radius)
    {
        return effective_world_radius <= 0.0f;
    }

    inline bool packed_mesh_anchor_world_radius_contract_valid(float effective_world_radius,
                                                               float brush_uv_radius)
    {
        if (packed_mesh_anchor_requests_world_radius_conversion(effective_world_radius))
        {
            return true;
        }
        // A derived radius is expressed in world units and must be finite and
        // materially distinct from the normalized UV radius.  This rejects the
        // historical bug that copied BrushSettings.Radius into both fields.
        return std::isfinite(effective_world_radius) &&
               std::isfinite(brush_uv_radius) &&
               brush_uv_radius > 0.0f &&
               effective_world_radius > brush_uv_radius;
    }

    inline bool resolve_packed_triangle_world_radius(double world_units_per_uv,
                                                     float brush_uv_radius,
                                                     float& world_radius)
    {
        world_radius = 0.0f;
        if (!std::isfinite(world_units_per_uv) || world_units_per_uv <= 0.0 ||
            !std::isfinite(brush_uv_radius) || brush_uv_radius <= 0.0f ||
            brush_uv_radius > 1.0f)
        {
            return false;
        }
        const double resolved = world_units_per_uv * static_cast<double>(brush_uv_radius);
        if (!std::isfinite(resolved) || resolved <= static_cast<double>(brush_uv_radius))
        {
            return false;
        }
        world_radius = static_cast<float>(resolved);
        return std::isfinite(world_radius) && world_radius > brush_uv_radius;
    }

    // BrushSettings.Radius is kept in planner/direct-UV units.  Only the packed
    // wire representation receives the mesh calibration so research direct and
    // internal-common routes cannot accidentally inherit the packed receiver's
    // normalization.  Returning the rounded float lets validation and encoding
    // use the exact same value without mutating the source stroke.
    inline bool resolve_packed_wire_brush_radius(float source_uv_radius,
                                                 double packed_wire_scale,
                                                 float& wire_uv_radius)
    {
        wire_uv_radius = 0.0f;
        if (!std::isfinite(source_uv_radius) || source_uv_radius <= 0.0f ||
            source_uv_radius > 1.0f || !std::isfinite(packed_wire_scale) ||
            packed_wire_scale <= 0.0)
        {
            return false;
        }
        const double scaled = static_cast<double>(source_uv_radius) * packed_wire_scale;
        if (!std::isfinite(scaled) || scaled <= 0.0 || scaled > 1.0)
        {
            return false;
        }
        const float rounded = static_cast<float>(scaled);
        if (!std::isfinite(rounded) || rounded <= 0.0f || rounded > 1.0f)
        {
            return false;
        }
        wire_uv_radius = rounded;
        return true;
    }

    // The compact decoder copies these fields verbatim into FPaintStroke before
    // running the skeletal-stroke preflight.  A non-positive value asks that
    // preflight to populate the component's native subdivision contract.  Do
    // not synthesize brush diameter or texture size into these wire fields.
    constexpr bool packed_mesh_anchor_requests_native_subdivision_preflight(
        int effective_subdivision_level,
        float effective_subdivision_pixel_size,
        int effective_template_resolution)
    {
        return effective_subdivision_level <= 0 &&
               effective_subdivision_pixel_size <= 0.0f &&
               effective_template_resolution <= 0;
    }

    // Receiver order in packed record subdivision tail (decoded into compact-stroke
    // fields at later offsets): subdivision level u8, subdivision pixel size
    // u8, template resolution u16 LE.  Tail sits at bytes 23..26 on 27-byte
    // records and 27..30 once the emissive FColor field is present.
    constexpr std::array<std::uint8_t, 4> packed_mesh_anchor_auto_subdivision_tail()
    {
        return {0, 0, 0, 0};
    }

    // UObject flags are checked against Shipping disassembly for the supported
    // build.  0x20000000 is intentionally not rejected: it is not an object
    // destruction flag and treating it as one spuriously blocks live objects.
    constexpr std::uint32_t RFClassDefaultObject = 0x00000010u;
    constexpr std::uint32_t RFBeginDestroyed = 0x00008000u;
    constexpr std::uint32_t RFFinishDestroyed = 0x00010000u;
    constexpr std::uint32_t RFMirroredGarbage = 0x40000000u;
    constexpr std::uint32_t ObjectRejectMask =
        RFClassDefaultObject | RFBeginDestroyed | RFFinishDestroyed | RFMirroredGarbage;
    constexpr std::uint32_t ClassRejectMask = RFBeginDestroyed | RFFinishDestroyed | RFMirroredGarbage;

    constexpr bool uobject_flags_usable(std::uint32_t object_flags, std::uint32_t class_flags)
    {
        return (object_flags & ObjectRejectMask) == 0 && (class_flags & ClassRejectMask) == 0;
    }

    constexpr bool packed_manager_precommit_matches(std::uintptr_t captured_manager,
                                                     std::uintptr_t resolved_manager)
    {
        return captured_manager != 0 && resolved_manager == captured_manager;
    }

    constexpr bool paired_paint_cancel_safe_to_observe(bool paired_mode,
                                                       int server_strokes_sent,
                                                       int local_strokes_submitted)
    {
        return !paired_mode || server_strokes_sent == local_strokes_submitted;
    }

    struct PacingDecision
    {
        int remote_batch_limit;
        int remote_delay_ms;
        int local_batch_limit;
        int local_delay_ms;
        bool used_contract_fallback;
    };

    constexpr int positive_or(int value, int fallback)
    {
        return value > 0 ? value : fallback;
    }

    constexpr int min_value(int left, int right)
    {
        return left < right ? left : right;
    }

    constexpr int max_value(int left, int right)
    {
        return left > right ? left : right;
    }

    constexpr int clamp_value(int value, int minimum, int maximum)
    {
        return min_value(max_value(value, minimum), maximum);
    }

    constexpr int ceil_div(int numerator, int denominator)
    {
        return denominator > 0 ? (numerator + denominator - 1) / denominator : numerator;
    }

    constexpr PacingDecision resolve_pacing(int requested_batch_limit,
                                            int requested_pacing_ms,
                                            int max_outgoing_strokes_per_batch,
                                            int max_outgoing_network_batches_per_second,
                                            int max_replicated_strokes_per_tick,
                                            int max_render_target_writes_per_frame)
    {
        const bool fallback = max_outgoing_strokes_per_batch <= 0 ||
                              max_outgoing_network_batches_per_second <= 0 ||
                              max_replicated_strokes_per_tick <= 0 ||
                              max_render_target_writes_per_frame <= 0;
        const int outgoing_strokes = positive_or(max_outgoing_strokes_per_batch, FallbackOutgoingStrokesPerBatch);
        const int outgoing_batches = positive_or(max_outgoing_network_batches_per_second, FallbackOutgoingBatchesPerSecond);
        const int replicated_strokes = positive_or(max_replicated_strokes_per_tick, FallbackReplicatedStrokesPerTick);
        const int render_writes = positive_or(max_render_target_writes_per_frame, FallbackRenderTargetWritesPerFrame);
        const int requested_batch = clamp_value(requested_batch_limit, 1, MaximumNetworkBatchLimit);
        const int requested_delay = clamp_value(requested_pacing_ms,
                                                MinimumNetworkPacingMs,
                                                MaximumManualNetworkPacingMs);
        const int automatic_remote_batch = min_value(requested_batch, min_value(outgoing_strokes, replicated_strokes));
        const int automatic_remote_delay = max_value(requested_delay,
                                                     max_value(MinimumNetworkPacingMs,
                                                               ceil_div(1000, outgoing_batches)));
        const int local_batch = min_value(InternalNoResendMaxCallsPerTick, render_writes);
        return {automatic_remote_batch,
                automatic_remote_delay,
                local_batch,
                FastLocalCadenceMs,
                fallback};
    }

    constexpr PacingDecision resolve_configured_pacing(
        bool auto_adapt,
        int requested_batch_limit,
        int requested_pacing_ms,
        int max_outgoing_strokes_per_batch,
        int max_outgoing_network_batches_per_second,
        int max_replicated_strokes_per_tick,
        int max_render_target_writes_per_frame)
    {
        if (auto_adapt)
        {
            return resolve_pacing(MaximumNetworkBatchLimit,
                                  MinimumNetworkPacingMs,
                                  max_outgoing_strokes_per_batch,
                                  max_outgoing_network_batches_per_second,
                                  max_replicated_strokes_per_tick,
                                  max_render_target_writes_per_frame);
        }
        const int manual_delay_ms = clamp_value(requested_pacing_ms,
                                                MinimumNetworkPacingMs,
                                                MaximumManualNetworkPacingMs);
        return {clamp_value(requested_batch_limit, 1, MaximumNetworkBatchLimit),
                manual_delay_ms,
                min_value(InternalNoResendMaxCallsPerTick,
                          positive_or(max_render_target_writes_per_frame, FallbackRenderTargetWritesPerFrame)),
                min_value(FastLocalCadenceMs, manual_delay_ms),
                false};
    }

    constexpr bool production_paint_uses_texture_import(bool auto_adapt,
                                                        bool normal_paint_requires_packed,
                                                        bool local_visual_sync_requested,
                                                        bool research_artifacts)
    {
        (void)auto_adapt;
        return normal_paint_requires_packed &&
               local_visual_sync_requested &&
               !research_artifacts;
    }

    constexpr int incremental_texture_import_chunk_limit(int server_batch_limit)
    {
        return max_value(IncrementalTextureImportMinimumStrokes,
                         clamp_value(server_batch_limit, 1, MaximumNetworkBatchLimit));
    }

    constexpr std::size_t incremental_texture_import_count(std::size_t server_offset,
                                                           std::size_t local_offset,
                                                           std::size_t total_strokes,
                                                           std::size_t max_strokes,
                                                           std::size_t pass_boundary)
    {
        const std::size_t submitted = std::min(server_offset, total_strokes);
        const std::size_t imported = std::min(local_offset, total_strokes);
        if (submitted <= imported)
        {
            return 0;
        }
        const std::size_t available = submitted - imported;
        const std::size_t bounded_max = std::max<std::size_t>(1, max_strokes);
        const std::size_t boundary = std::min(pass_boundary, total_strokes);
        const std::size_t before_boundary = boundary > imported
                                                ? boundary - imported
                                                : available;
        return std::min(available, std::min(bounded_max, before_boundary));
    }

    constexpr bool server_only_replay_complete(bool local_visual_sync_enabled,
                                               bool local_texture_import_started,
                                               bool server_texture_sync_started,
                                               bool server_packed_fallback,
                                               int server_batch_failures,
                                               int server_strokes_sent,
                                               int total_strokes)
    {
        return !local_visual_sync_enabled &&
               (server_packed_fallback ||
                (!local_texture_import_started && !server_texture_sync_started)) &&
               server_batch_failures == 0 &&
               server_strokes_sent == total_strokes;
    }

    constexpr bool uses_fast_local_dispatch_wakeup(bool internal_no_resend_local_apply,
                                                   bool local_packed_queue,
                                                   bool local_work_remaining)
    {
        return internal_no_resend_local_apply &&
               !local_packed_queue &&
               local_work_remaining;
    }

    constexpr bool should_immediately_repost_direct_local(
        bool fast_local_dispatch_wakeup,
        int immediate_reposts_since_deferred_wakeup)
    {
        return fast_local_dispatch_wakeup &&
               immediate_reposts_since_deferred_wakeup <
                   DirectLocalImmediateRepostsPerDeferredWakeup;
    }

    constexpr double parallel_lane_eta_ms(double server_eta_ms, double local_eta_ms)
    {
        return server_eta_ms < 0.0 || local_eta_ms < 0.0
                   ? -1.0
                   : (server_eta_ms > local_eta_ms ? server_eta_ms : local_eta_ms);
    }

    // EPaintChannel: 0..3 address one render target, All addresses four,
    // AlbedoMetallicRoughness addresses three, Emissive addresses one, and
    // AlbedoMetallicRoughnessEmissive addresses four.  The game limit is
    // expressed in render-target writes, not paint-stroke calls.
    constexpr int paint_channel_write_cost(int target_channel)
    {
        if (target_channel == 4)
        {
            return 4;
        }
        if (target_channel == 5)
        {
            return 3;
        }
        if (target_channel == 7)
        {
            return 4;
        }
        return 1;
    }

    constexpr bool local_dispatch_can_append(int processed_calls,
                                             int scheduled_writes,
                                             int next_write_cost,
                                             int max_calls,
                                             int max_render_target_writes)
    {
        if (processed_calls <= 0)
        {
            return true;
        }
        return processed_calls < max_value(1, max_calls) &&
               scheduled_writes + max_value(1, next_write_cost) <=
                   max_value(1, max_render_target_writes);
    }

    constexpr bool local_dispatch_cpu_budget_reached(int processed_calls,
                                                     std::uint64_t elapsed_us)
    {
        return processed_calls > 0 && elapsed_us >= LocalDispatchCpuBudgetUs;
    }

    constexpr int recurring_scheduler_delay_ms(int requested_delay_ms)
    {
        return max_value(1, requested_delay_ms);
    }

    // The paired packed path pipelines server/local batches while the exact
    // component queue drains asynchronously.  Allow two configured batches of
    // headroom so a full first batch (which may coalesce below the wire count)
    // does not stall the stream until the receiver catches up.
    constexpr int PairedLocalQueuePipelineBatchMultiplier = 2;

    constexpr int paired_local_queue_available_capacity(int configured_batch_limit,
                                                        int queued_strokes,
                                                        int strokes_submitted = 0,
                                                        int drained_strokes = 0)
    {
        if (queued_strokes < 0)
        {
            return 0;
        }
        const int limit =
            max_value(1, configured_batch_limit) * PairedLocalQueuePipelineBatchMultiplier;
        const int queue_room = max_value(0, limit - queued_strokes);
        if (strokes_submitted <= 0)
        {
            return queue_room;
        }
        const int in_flight =
            max_value(0, strokes_submitted - max_value(0, drained_strokes));
        const int logical_room = max_value(0, limit - in_flight);
        return max_value(queue_room, logical_room);
    }

    constexpr int paired_local_queue_commit_count(int requested_strokes,
                                                  int configured_batch_limit,
                                                  int queued_strokes,
                                                  int strokes_submitted = 0,
                                                  int drained_strokes = 0)
    {
        return min_value(max_value(0, requested_strokes),
                         paired_local_queue_available_capacity(configured_batch_limit,
                                                               queued_strokes,
                                                               strokes_submitted,
                                                               drained_strokes));
    }

    constexpr bool paired_local_queue_postcondition_ok(int observed_queue_delta,
                                                       int submitted_stroke_count)
    {
        return observed_queue_delta > 0 &&
               observed_queue_delta <= max_value(0, submitted_stroke_count);
    }

    constexpr bool paired_local_queue_cancel_needs_drain(bool cancel_requested,
                                                         int queued_strokes)
    {
        return cancel_requested && queued_strokes > 0;
    }

    // The supported manager appends strokes in order and advances one processed
    // cursor.  Once a job owns an initially empty component queue, submitted -
    // queued is therefore a conservative render-queue cursor.  Preserve the
    // previous value so a transient foreign queue increase cannot move UI pass
    // progress backwards.
    constexpr int receiver_queue_rendered_strokes(int submitted_strokes,
                                                   int queued_strokes,
                                                   int previous_rendered_strokes)
    {
        const int submitted = max_value(0, submitted_strokes);
        const int queued = max_value(0, queued_strokes);
        const int observed = clamp_value(submitted - queued, 0, submitted);
        return clamp_value(max_value(previous_rendered_strokes, observed), 0, submitted);
    }

    constexpr bool receiver_queue_drain_complete(int queued_strokes,
                                                  int consecutive_zero_observations)
    {
        return queued_strokes == 0 && consecutive_zero_observations >= 2;
    }

    constexpr bool receiver_queue_idle_threshold_reached(int queued_strokes,
                                                          std::uint64_t idle_ms,
                                                          std::uint64_t threshold_ms)
    {
        return queued_strokes > 0 && threshold_ms > 0 && idle_ms >= threshold_ms;
    }

    struct SpatialScanlineKey
    {
        int row;
        double horizontal;
        std::size_t original_ordinal;
    };

    inline int spatial_scanline_row(double top_z, double point_z, double row_height)
    {
        if (!std::isfinite(top_z) || !std::isfinite(point_z) ||
            !std::isfinite(row_height) || row_height <= 0.000001)
        {
            return 0;
        }
        return static_cast<int>(std::floor(std::max(0.0, top_z - point_z) / row_height));
    }

    inline bool spatial_scanline_less(const SpatialScanlineKey& left,
                                      const SpatialScanlineKey& right)
    {
        if (left.row != right.row)
        {
            return left.row < right.row;
        }
        if (left.horizontal != right.horizontal)
        {
            return left.horizontal < right.horizontal;
        }
        return left.original_ordinal < right.original_ordinal;
    }

    enum class ReplayRegion
    {
        Front,
        Side,
        Back,
    };

    enum class ReplayRegionMode
    {
        Paint,
        Fill,
        Skip,
    };

    enum class ReplayPass
    {
        Fill,
        CoarsePaint,
        FinePaint,
        Complete,
    };

    struct ReplayPassWindow
    {
        ReplayPass pass;
        std::size_t begin;
        std::size_t end;
    };

    // Resolve the pass containing an offset in the effective replay stream.  The
    // planner stores exclusive boundaries, so an offset exactly at a boundary is
    // reported as the next pass.  Clamp malformed boundaries here so diagnostics
    // remain safe even when a runtime limit truncates the planned stream.
    constexpr ReplayPassWindow replay_pass_window(std::size_t offset,
                                                  std::size_t total,
                                                  std::size_t fill_end,
                                                  std::size_t coarse_end)
    {
        const std::size_t safe_fill_end = std::min(fill_end, total);
        const std::size_t safe_coarse_end = std::min(std::max(coarse_end, safe_fill_end), total);
        const std::size_t safe_offset = std::min(offset, total);
        if (safe_offset >= total)
        {
            return {ReplayPass::Complete, total, total};
        }
        if (safe_offset < safe_fill_end)
        {
            return {ReplayPass::Fill, 0, safe_fill_end};
        }
        if (safe_offset < safe_coarse_end)
        {
            return {ReplayPass::CoarsePaint, safe_fill_end, safe_coarse_end};
        }
        return {ReplayPass::FinePaint, safe_coarse_end, total};
    }

    struct TwoBrushReplayCandidate
    {
        std::size_t sample_index;
        ReplayRegion region;
        ReplayRegionMode mode;
        int uv_island;
        double u;
        double v;
        bool has_current_view_position;
        double current_view_vertical;
        double fallback_view_vertical;
        double horizontal;
        std::size_t original_ordinal;
    };

    struct TwoBrushReplayEntry
    {
        std::size_t sample_index;
        ReplayPass pass;
        ReplayRegion region;
        SpatialScanlineKey spatial_key;
    };

    struct TwoBrushReplayPlan
    {
        std::vector<TwoBrushReplayEntry> entries{};
        std::size_t fill_end{0};
        std::size_t coarse_end{0};
        std::size_t fill_count{0};
        std::size_t coarse_paint_count{0};
        std::size_t fine_paint_count{0};
        std::size_t fill_candidates{0};
        std::size_t fill_deduplicated{0};
        std::size_t coarse_paint_candidates{0};
        std::size_t coarse_paint_deduplicated{0};
        bool current_view_projection_fallback_used{false};
        std::size_t current_view_projection_fallback_candidates{0};
    };

    inline TwoBrushReplayPlan build_two_brush_replay_plan(
        const std::vector<TwoBrushReplayCandidate>& candidates,
        int texture_size,
        bool brush_1_enabled,
        double brush_1_size_texels,
        bool brush_2_enabled,
        double brush_2_size_texels,
        double fill_radius_texels)
    {
        TwoBrushReplayPlan plan{};
        const double texture_size_double = static_cast<double>(max_value(1, texture_size));
        const double fill_cell_uv = fill_radius_texels * 0.75 / texture_size_double;
        const double coarse_cell_uv = brush_1_size_texels / texture_size_double;
        bool fill_all_regions = false;
        for (const auto& candidate : candidates)
        {
            if (candidate.mode == ReplayRegionMode::Fill)
            {
                fill_all_regions = true;
                break;
            }
        }
        double vertical_top = 0.0;
        double vertical_bottom = 0.0;
        bool have_vertical_bounds = false;
        const auto selected_vertical = [](const TwoBrushReplayCandidate& candidate) {
            return candidate.has_current_view_position && std::isfinite(candidate.current_view_vertical)
                       ? candidate.current_view_vertical
                       : (std::isfinite(candidate.fallback_view_vertical)
                              ? candidate.fallback_view_vertical
                              : 0.0);
        };
        for (const auto& candidate : candidates)
        {
            if (!fill_all_regions && candidate.mode == ReplayRegionMode::Skip)
            {
                continue;
            }
            const double vertical = selected_vertical(candidate);
            if (!have_vertical_bounds)
            {
                vertical_top = vertical;
                vertical_bottom = vertical;
                have_vertical_bounds = true;
            }
            else
            {
                vertical_top = std::max(vertical_top, vertical);
                vertical_bottom = std::min(vertical_bottom, vertical);
            }
            if (!candidate.has_current_view_position || !std::isfinite(candidate.current_view_vertical))
            {
                ++plan.current_view_projection_fallback_candidates;
            }
        }
        plan.current_view_projection_fallback_used =
            plan.current_view_projection_fallback_candidates > 0;
        const double vertical_span = std::max(0.001, vertical_top - vertical_bottom);
        auto append_pass = [&](ReplayPass pass,
                               ReplayRegionMode required_mode,
                               double dedupe_cell_uv,
                               double row_size_texels,
                               bool include_all_regions = false) {
            std::set<std::tuple<int, int, int, int>> emitted_cells{};
            std::vector<TwoBrushReplayEntry> pending{};
            const double row_height = std::max(
                0.000001,
                vertical_span * std::max(0.001, row_size_texels) / texture_size_double);
            for (const auto& candidate : candidates)
            {
                if (!include_all_regions && candidate.mode != required_mode)
                {
                    continue;
                }
                if (pass == ReplayPass::Fill)
                {
                    ++plan.fill_candidates;
                }
                else if (pass == ReplayPass::CoarsePaint)
                {
                    ++plan.coarse_paint_candidates;
                }
                if (dedupe_cell_uv > 0.000001)
                {
                    const auto cell_coordinate = [&](double value) {
                        const double finite_value = std::isfinite(value) ? value : 0.0;
                        return static_cast<int>(std::floor(
                            std::max(0.0, std::min(1.0, finite_value)) / dedupe_cell_uv));
                    };
                    const auto cell = std::make_tuple(
                        static_cast<int>(candidate.region),
                        candidate.uv_island,
                        cell_coordinate(candidate.u),
                        cell_coordinate(candidate.v));
                    if (!emitted_cells.insert(cell).second)
                    {
                        if (pass == ReplayPass::Fill)
                        {
                            ++plan.fill_deduplicated;
                        }
                        else if (pass == ReplayPass::CoarsePaint)
                        {
                            ++plan.coarse_paint_deduplicated;
                        }
                        continue;
                    }
                }
                pending.push_back(
                    {candidate.sample_index,
                     pass,
                     candidate.region,
                     {spatial_scanline_row(vertical_top,
                                           selected_vertical(candidate),
                                           row_height),
                      candidate.horizontal,
                      candidate.original_ordinal}});
            }
            std::stable_sort(pending.begin(), pending.end(), [](const auto& left, const auto& right) {
                return spatial_scanline_less(left.spatial_key, right.spatial_key);
            });
            plan.entries.insert(plan.entries.end(), pending.begin(), pending.end());
        };

        append_pass(ReplayPass::Fill,
                    ReplayRegionMode::Fill,
                    fill_cell_uv,
                    fill_radius_texels,
                    fill_all_regions);
        plan.fill_end = plan.entries.size();
        plan.fill_count = plan.fill_end;
        if (brush_1_enabled)
        {
            append_pass(ReplayPass::CoarsePaint,
                        ReplayRegionMode::Paint,
                        coarse_cell_uv,
                        brush_1_size_texels);
        }
        plan.coarse_end = plan.entries.size();
        plan.coarse_paint_count = plan.coarse_end - plan.fill_end;
        if (brush_2_enabled)
        {
            append_pass(ReplayPass::FinePaint,
                        ReplayRegionMode::Paint,
                        0.0,
                        brush_2_size_texels);
        }
        plan.fine_paint_count = plan.entries.size() - plan.coarse_end;
        return plan;
    }

    constexpr bool event_watch_generation_active(bool enabled,
                                                 std::uint64_t current_generation,
                                                 std::uint64_t captured_generation)
    {
        return enabled && current_generation == captured_generation;
    }

    constexpr bool requires_internal_no_resend(bool preview_only,
                                                bool unpreview_only,
                                                bool research_artifacts,
                                                bool research_combined_no_resend)
    {
        return !preview_only &&
               !unpreview_only &&
               research_artifacts &&
               research_combined_no_resend;
    }
}
