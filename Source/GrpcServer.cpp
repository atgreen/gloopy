// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "GrpcServer.h"
#include "MainComponent.h"

#include <grpcpp/grpcpp.h>
#include "gloopy.grpc.pb.h"

#include <thread>
#include <chrono>
#include <vector>
#include <string>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
namespace pb = gloopy::v1;

namespace
{
    juce::String js (const std::string& s) { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }

    void fillInsert (pb::MixerInsert* mi, const MainComponent::InsertSnap& s)
    {
        mi->set_index (s.index); mi->set_name (s.name.toStdString());
        mi->set_volume (s.volume); mi->set_pan (s.pan); mi->set_mute (s.mute); mi->set_solo (s.solo);
        mi->set_is_bus (s.isBus);
        for (auto& e : s.effects)
        {
            auto* ei = mi->add_effects();
            ei->set_slot (e.slot); ei->set_name (e.name.toStdString()); ei->set_bypassed (e.bypassed);
        }
        for (auto& sd : s.sends)
        {
            auto* o = mi->add_sends();
            o->set_bus (sd.first); o->set_level (sd.second);
        }
    }

    void fillPlugin (pb::PluginInfo* pi, const MainComponent::PluginSnap& s)
    {
        pi->set_name (s.name.toStdString());
        pi->set_format (s.format.toStdString());
        pi->set_is_instrument (s.isInstrument);
        pi->set_identifier (s.identifier.toStdString());
        pi->set_vendor (s.vendor.toStdString());
        pi->set_category (s.category.toStdString());
        pi->set_version (s.version.toStdString());
        pi->set_num_inputs (s.numInputs);
        pi->set_num_outputs (s.numOutputs);
    }

    class ServiceImpl final : public pb::Gloopy::Service
    {
    public:
        explicit ServiceImpl (MainComponent& mc) : main (mc) {}

        // ---- transport ----
        Status Play (ServerContext*, const pb::Empty*, pb::Ack* r) override
        { main.apiPlay(); r->set_ok (true); return Status::OK; }

        Status Stop (ServerContext*, const pb::Empty*, pb::Ack* r) override
        { main.apiStop(); r->set_ok (true); return Status::OK; }

        Status SetTempo (ServerContext*, const pb::Tempo* q, pb::Ack* r) override
        { main.apiSetTempo (q->bpm()); r->set_ok (true); return Status::OK; }

        Status SetSwing (ServerContext*, const pb::Swing* q, pb::Ack* r) override
        { main.apiSetSwing (q->amount()); r->set_ok (true); return Status::OK; }

        Status Seek (ServerContext*, const pb::Position* q, pb::Ack* r) override
        { main.apiSeek (q->beats()); r->set_ok (true); return Status::OK; }

        Status StartRecording (ServerContext*, const pb::Empty*, pb::Ack* r) override
        { main.apiStartRecording(); r->set_ok (true); return Status::OK; }

        Status StopRecording (ServerContext*, const pb::Empty*, pb::Ack* r) override
        { main.apiStopRecording(); r->set_ok (true); return Status::OK; }

        Status ListAudioInputs (ServerContext*, const pb::Empty*, pb::AudioInputs* r) override
        { for (auto& n : main.apiListAudioInputs()) r->add_names (n.toStdString()); return Status::OK; }

        Status ArmTrack (ServerContext*, const pb::ArmRequest* q, pb::Ack* r) override
        { const bool ok = main.apiArmTrack (q->track_id(), q->armed(), q->input(), q->channels(), q->monitor());
          r->set_ok (ok); if (! ok) r->set_error ("track not found"); return Status::OK; }

        Status SetPunchRange (ServerContext*, const pb::PunchRange* q, pb::Ack* r) override
        { r->set_ok (main.apiSetPunchRange (q->enabled(), q->in_beat(), q->out_beat(), q->count_in_beats()));
          return Status::OK; }

        Status SetLoop (ServerContext*, const pb::Loop* q, pb::Ack* r) override
        { main.apiSetLoop (q->enabled(), q->start_beat(), q->end_beat()); r->set_ok (true); return Status::OK; }

        Status SetRecordSettings (ServerContext*, const pb::RecordSettings* q, pb::Ack* r) override
        { r->set_ok (main.apiSetRecordSettings (q->format(), q->latency_offset_seconds())); return Status::OK; }

        Status PromoteTake (ServerContext*, const pb::TakeRef* q, pb::Ack* r) override
        { const bool ok = main.apiPromoteTake (js (q->take_id()));
          r->set_ok (ok); if (! ok) r->set_error ("take not found in raw/"); return Status::OK; }

        Status CleanupTakes (ServerContext*, const pb::Empty*, pb::TakeCount* r) override
        { r->set_count (main.apiCleanupTakes()); return Status::OK; }

        Status RecoverTakes (ServerContext*, const pb::Empty*, pb::TakeCount* r) override
        { r->set_count (main.apiRecoverTakes()); return Status::OK; }

        Status GetTransport (ServerContext*, const pb::Empty*, pb::TransportState* r) override
        {
            auto s = main.apiGetTransport();
            r->set_playing (s.playing);
            r->set_bpm (s.bpm);
            r->set_position_beats (s.positionBeats);
            r->set_loop_enabled (s.loopEnabled);
            r->set_loop_start (s.loopStart);
            r->set_loop_end (s.loopEnd);
            r->set_metronome (main.apiGetMetronome());
            r->set_swing (s.swing);
            return Status::OK;
        }

        // ---- tracks ----
        Status AddSynthTrack (ServerContext*, const pb::AddSynthTrackRequest* q, pb::TrackId* r) override
        {
            const int id = main.apiAddSynthTrack (js (q->name()), (int) q->wave(),
                                                  q->attack(), q->decay(), q->sustain(), q->release(), q->gain());
            r->set_id (id);
            return Status::OK;
        }

        Status SetTrackParams (ServerContext*, const pb::TrackParams* q, pb::Ack* r) override
        {
            const bool ok = main.apiSetTrackParams (q->id(),
                                q->has_volume(), q->volume(), q->has_pan(), q->pan(),
                                q->has_mute(),   q->mute(),   q->has_solo(), q->solo(),
                                q->has_name(),   js (q->name()));
            r->set_ok (ok);
            if (! ok) r->set_error ("track not found");
            return Status::OK;
        }

        Status SetSynthParam (ServerContext*, const pb::SynthParamSet* q, pb::Ack* r) override
        {
            const bool ok = main.apiSetSynthParam (q->track_id(), js (q->name()), q->value());
            r->set_ok (ok);
            if (! ok) r->set_error ("no synth track / unknown param");
            return Status::OK;
        }

        Status ListPresets (ServerContext*, const pb::PresetCategory* q, pb::PresetList* r) override
        { for (auto& n : main.apiListPresets (js (q->category()))) r->add_names (n.toStdString()); return Status::OK; }

        Status SaveSynthPreset (ServerContext*, const pb::PresetRef* q, pb::Ack* r) override
        { const bool ok = main.apiSaveSynthPreset (q->target(), js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("not a synth track"); return Status::OK; }

        Status LoadSynthPreset (ServerContext*, const pb::PresetRef* q, pb::Ack* r) override
        { const bool ok = main.apiLoadSynthPreset (q->target(), js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("preset or synth track not found"); return Status::OK; }

        Status SaveInstrumentPreset (ServerContext*, const pb::PresetRef* q, pb::Ack* r) override
        { const bool ok = main.apiSaveInstrumentPreset (q->target(), js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("unsupported instrument"); return Status::OK; }

        Status LoadInstrumentPreset (ServerContext*, const pb::PresetRef* q, pb::Ack* r) override
        { const bool ok = main.apiLoadInstrumentPreset (q->target(), js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("preset or track not found"); return Status::OK; }

        Status SaveEffectPreset (ServerContext*, const pb::PresetRef* q, pb::Ack* r) override
        { const bool ok = main.apiSaveEffectPreset (q->target(), js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("bad insert"); return Status::OK; }

        Status LoadEffectPreset (ServerContext*, const pb::PresetRef* q, pb::Ack* r) override
        { const bool ok = main.apiLoadEffectPreset (q->target(), js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("preset or insert not found"); return Status::OK; }

        Status ListTracks (ServerContext*, const pb::Empty*, pb::TrackList* r) override
        {
            for (auto& t : main.apiListTracks())
            {
                auto* ti = r->add_tracks();
                ti->set_id (t.id);
                ti->set_name (t.name.toStdString());
                ti->set_type (t.type.toStdString());
                ti->set_volume (t.volume);
                ti->set_pan (t.pan);
                ti->set_mute (t.mute);
                ti->set_clips (t.clips);
                ti->set_colour (t.colour.toStdString());
                ti->set_polarity (t.polarity);
            }
            return Status::OK;
        }

        // ---- clips ----
        Status AddClip (ServerContext*, const pb::AddClipRequest* q, pb::ClipId* r) override
        {
            std::vector<Note> notes;
            notes.reserve ((size_t) q->notes_size());
            for (int i = 0; i < q->notes_size(); ++i)
            {
                const auto& n = q->notes (i);
                const float prob = n.probability() > 0.0f ? juce::jmin (1.0f, n.probability()) : 1.0f;   // proto3 omits 1.0? no; unset=0 -> full
                notes.push_back ({ n.pitch(), n.start_beat(), n.length_beats(), n.velocity(), prob });
            }
            const int idx = main.apiAddClip (q->track_id(), q->start_beat(), q->length_beats(),
                                             q->content_len_beats(), q->looped(), notes, js (q->name()));
            r->set_track_id (q->track_id());
            r->set_index (idx);
            return Status::OK;
        }

        // ---- mixer / effects ----
        Status ListInserts (ServerContext*, const pb::Empty*, pb::InsertList* r) override
        {
            for (auto& ins : main.apiListInserts()) fillInsert (r->add_inserts(), ins);
            return Status::OK;
        }

        Status SetInsertParams (ServerContext*, const pb::InsertParams* q, pb::Ack* r) override
        {
            const bool ok = main.apiSetInsertParams (q->index(),
                                q->has_volume(), q->volume(), q->has_pan(), q->pan(),
                                q->has_mute(),   q->mute(),   q->has_solo(), q->solo());
            r->set_ok (ok); if (! ok) r->set_error ("insert not found");
            return Status::OK;
        }

        Status SetInsertName (ServerContext*, const pb::InsertName* q, pb::Ack* r) override
        { const bool ok = main.apiSetInsertName (q->index(), js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("rename failed (insert not found or empty name)"); return Status::OK; }

        Status AddEffect (ServerContext*, const pb::AddEffectRequest* q, pb::EffectRef* r) override
        {
            const int slot = main.apiAddEffect (q->insert(), (int) q->type());
            r->set_insert (q->insert()); r->set_slot (slot);
            return Status::OK;
        }

        Status RemoveEffect (ServerContext*, const pb::EffectRef* q, pb::Ack* r) override
        {
            const bool ok = main.apiRemoveEffect (q->insert(), q->slot());
            r->set_ok (ok); if (! ok) r->set_error ("effect not found");
            return Status::OK;
        }

        Status SetEffectParam (ServerContext*, const pb::EffectParamSet* q, pb::Ack* r) override
        {
            const bool ok = main.apiSetEffectParam (q->insert(), q->slot(), js (q->name()), q->value());
            r->set_ok (ok); if (! ok) r->set_error ("effect/param not found");
            return Status::OK;
        }

        Status SetEffectBypass (ServerContext*, const pb::EffectBypassSet* q, pb::Ack* r) override
        {
            const bool ok = main.apiSetEffectBypass (q->insert(), q->slot(), q->bypassed());
            r->set_ok (ok); if (! ok) r->set_error ("effect not found");
            return Status::OK;
        }

        Status GetEffectParams (ServerContext*, const pb::EffectRef* q, pb::ParamList* r) override
        {
            for (auto& p : main.apiGetEffectParams (q->insert(), q->slot()))
            {
                auto* pp = r->add_params();
                pp->set_name (p.name.toStdString()); pp->set_value (p.value);
                pp->set_min (p.min); pp->set_max (p.max);
            }
            return Status::OK;
        }

        // ---- scales ----
        Status SetScale (ServerContext*, const pb::Scale* q, pb::Ack* r) override
        {
            std::vector<int> iv; for (int i = 0; i < q->intervals_size(); ++i) iv.push_back (q->intervals (i));
            const bool ok = main.apiSetScale (q->root(), js (q->name()), iv);
            r->set_ok (ok); if (! ok) r->set_error ("unknown scale name and no intervals given");
            return Status::OK;
        }
        Status GetScale (ServerContext*, const pb::Empty*, pb::Scale* r) override
        {
            int root; juce::String name; std::vector<int> iv;
            main.apiGetScale (root, name, iv);
            r->set_root (root); r->set_name (name.toStdString());
            for (int i : iv) r->add_intervals (i);
            return Status::OK;
        }
        Status SnapClipToScale (ServerContext*, const pb::ClipRef* q, pb::Ack* r) override
        { const int n = main.apiSnapClipToScale (q->track_id(), q->index());
          r->set_ok (n >= 0); if (n < 0) r->set_error ("clip not found"); return Status::OK; }
        Status SetTuning (ServerContext*, const pb::Tuning* q, pb::Ack* r) override
        {
            std::vector<double> c; for (int i = 0; i < q->cents_size(); ++i) c.push_back (q->cents (i));
            const bool ok = main.apiSetTuning (c);
            r->set_ok (ok); if (! ok) r->set_error ("tuning needs exactly 12 cents values"); return Status::OK;
        }
        Status GetTuning (ServerContext*, const pb::Empty*, pb::Tuning* r) override
        { for (double c : main.apiGetTuning()) r->add_cents (c); return Status::OK; }
        Status ImportScl (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        { const bool ok = main.apiImportScl (js (q->path()));
          r->set_ok (ok); if (! ok) r->set_error ("could not parse a 12-note .scl"); return Status::OK; }

        // ---- buses & sends ----
        Status AddBus (ServerContext*, const pb::AddBusRequest* q, pb::TrackId* r) override
        { r->set_id (main.apiAddBus (js (q->name()))); return Status::OK; }
        Status RemoveBus (ServerContext*, const pb::TrackId* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveBus (q->id());
          r->set_ok (ok); if (! ok) r->set_error ("remove bus failed (index is not a bus)"); return Status::OK; }
        Status SetSend (ServerContext*, const pb::SetSendRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetSend (q->insert(), q->bus(), q->level(), q->post_fader());
          r->set_ok (ok); if (! ok) r->set_error ("invalid send (bad insert/bus, or nothing to remove)"); return Status::OK; }

        // ---- control groups (VCA-lite) ----
        Status DefineControlGroup (ServerContext*, const pb::ControlGroupGain* q, pb::Ack* r) override
        { const bool ok = main.apiDefineControlGroup (js (q->name()), q->gain());
          r->set_ok (ok); if (! ok) r->set_error ("invalid group name"); return Status::OK; }
        Status SetControlGroupGain (ServerContext*, const pb::ControlGroupGain* q, pb::Ack* r) override
        { const bool ok = main.apiSetControlGroupGain (js (q->name()), q->gain());
          r->set_ok (ok); if (! ok) r->set_error ("no such control group"); return Status::OK; }
        Status SetControlGroupMute (ServerContext*, const pb::ControlGroupMute* q, pb::Ack* r) override
        { const bool ok = main.apiSetControlGroupMute (js (q->name()), q->mute());
          r->set_ok (ok); if (! ok) r->set_error ("no such control group"); return Status::OK; }
        Status SetControlGroupSolo (ServerContext*, const pb::ControlGroupSolo* q, pb::Ack* r) override
        { const bool ok = main.apiSetControlGroupSolo (js (q->name()), q->solo());
          r->set_ok (ok); if (! ok) r->set_error ("no such control group"); return Status::OK; }
        Status AssignInsertToGroup (ServerContext*, const pb::GroupAssign* q, pb::Ack* r) override
        { const bool ok = main.apiAssignInsertToGroup (q->insert(), js (q->group()));
          r->set_ok (ok); if (! ok) r->set_error ("invalid insert index"); return Status::OK; }
        Status RemoveControlGroup (ServerContext*, const pb::GroupName* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveControlGroup (js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("no such control group"); return Status::OK; }
        Status ListControlGroups (ServerContext*, const pb::Empty*, pb::ControlGroupList* r) override
        {
            for (auto& g : main.apiListControlGroups())
            { auto* o = r->add_groups(); o->set_name (g.name.toStdString());
              o->set_gain (g.gain); o->set_mute (g.mute); o->set_members (g.members); o->set_solo (g.solo); }
            return Status::OK;
        }

        // ---- tempo map ----
        Status AddTempoMarker (ServerContext*, const pb::TempoMarker* q, pb::Ack* r) override
        { const bool ok = main.apiAddTempoMarker (q->beat(), q->bpm());
          r->set_ok (ok); if (! ok) r->set_error ("invalid tempo marker (beat>=0, bpm 20..400)"); return Status::OK; }
        Status RemoveTempoMarker (ServerContext*, const pb::TempoMarker* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveTempoMarker (q->beat());
          r->set_ok (ok); if (! ok) r->set_error ("no marker at that beat"); return Status::OK; }
        Status ListTempoMarkers (ServerContext*, const pb::Empty*, pb::TempoMap* r) override
        {
            for (auto& mk : main.apiListTempoMarkers())
            { auto* o = r->add_markers(); o->set_beat (mk.beat); o->set_bpm (mk.bpm); }
            return Status::OK;
        }
        Status BeatsToSeconds (ServerContext*, const pb::Position* q, pb::SecondsValue* r) override
        { r->set_seconds (main.apiBeatsToSeconds (q->beats())); return Status::OK; }
        Status SecondsToBeats (ServerContext*, const pb::SecondsValue* q, pb::Position* r) override
        { r->set_beats (main.apiSecondsToBeats (q->seconds())); return Status::OK; }
        Status SetTimeSignature (ServerContext*, const pb::TimeSignature* q, pb::Ack* r) override
        { const bool ok = main.apiSetTimeSignature (q->numerator(), q->denominator());
          r->set_ok (ok); if (! ok) r->set_error ("invalid time signature (1..32 / 1..32)"); return Status::OK; }
        Status GetTimeSignature (ServerContext*, const pb::Empty*, pb::TimeSignature* r) override
        { int n = 4, d = 4; main.apiGetTimeSignature (n, d);
          r->set_numerator (n); r->set_denominator (d); r->set_beats_per_bar ((double) n * 4.0 / (double) d); return Status::OK; }
        Status BeatsToBarBeat (ServerContext*, const pb::BeatPos* q, pb::BarBeat* r) override
        { int bar = 1; double bib = 1.0; main.apiBeatsToBarBeat (q->beat(), bar, bib);
          r->set_bar (bar); r->set_beat_in_bar (bib); return Status::OK; }
        Status BarBeatToBeats (ServerContext*, const pb::BarBeat* q, pb::BeatPos* r) override
        { r->set_beat (main.apiBarBeatToBeats (q->bar(), q->beat_in_bar())); return Status::OK; }

        // ---- mixer scenes ----
        Status DefineMixerScene (ServerContext*, const pb::SceneName* q, pb::Ack* r) override
        { const bool ok = main.apiDefineMixerScene (js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("invalid scene name"); return Status::OK; }
        Status ListMixerScenes (ServerContext*, const pb::Empty*, pb::SceneList* r) override
        { for (auto& n : main.apiListMixerScenes()) r->add_names (n.toStdString()); return Status::OK; }
        Status RecallMixerScene (ServerContext*, const pb::SceneName* q, pb::Ack* r) override
        { const bool ok = main.apiRecallMixerScene (js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("scene not found"); return Status::OK; }
        Status RemoveMixerScene (ServerContext*, const pb::SceneName* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveMixerScene (js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("scene not found"); return Status::OK; }

        // ---- automation ----
        Status SetAutomation (ServerContext*, const pb::Automation* q, pb::Ack* r) override
        {
            std::vector<MainComponent::AutoPointSnap> pts;
            for (int i = 0; i < q->points_size(); ++i)
                pts.push_back ({ q->points (i).beat(), q->points (i).value() });
            if (! q->param_id().empty())   // id-addressed lane (the unified path)
                main.apiSetAutomationById (js (q->param_id()), pts);
            else
                main.apiSetAutomation ((int) q->target(), q->id(), q->slot(), js (q->param()), pts);
            r->set_ok (true);
            return Status::OK;
        }

        Status AddAutomationPoint (ServerContext*, const pb::AddAutoPointRequest* q, pb::Ack* r) override
        { const bool ok = main.apiAddAutomationPointById (js (q->param_id()), q->beat(), q->value());
          r->set_ok (ok); if (! ok) r->set_error ("invalid param id"); return Status::OK; }
        Status SetAutomationStep (ServerContext*, const pb::AutoStepRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetAutomationStep (js (q->param_id()), q->step());
          r->set_ok (ok); if (! ok) r->set_error ("no automation lane on that param"); return Status::OK; }
        Status SetAutomationCurve (ServerContext*, const pb::AutoCurveRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetAutomationCurve (js (q->param_id()), q->curve());
          r->set_ok (ok); if (! ok) r->set_error ("no automation lane on that param"); return Status::OK; }

        Status GetAutomation (ServerContext*, const pb::Empty*, pb::AutomationList* r) override
        {
            for (auto& lane : main.apiGetAutomation())
            {
                auto* a = r->add_lanes();
                a->set_target ((pb::AutoTarget) lane.type); a->set_id (lane.id);
                a->set_slot (lane.slot); a->set_param (lane.param.toStdString());
                a->set_param_id (lane.target.toStdString());
                for (auto& p : lane.points) { auto* pt = a->add_points(); pt->set_beat (p.beat); pt->set_value (p.value); }
            }
            return Status::OK;
        }

        // ---- modulation matrix ----
        Status SetModulation (ServerContext*, const pb::ModRoute* q, pb::Ack* r) override
        { const bool ok = main.apiSetModulation (js (q->target()), q->rate(), q->depth(), q->shape(), q->center(),
                                                 q->sync_beats(), q->phase(), q->unipolar(), q->slew_ms());
          r->set_ok (ok); if (! ok) r->set_error ("invalid modulation target"); return Status::OK; }
        Status AddModulation (ServerContext*, const pb::ModRoute* q, pb::Ack* r) override
        { const bool ok = main.apiAddModulation (js (q->target()), q->rate(), q->depth(), q->shape(), q->center(),
                                                 q->sync_beats(), q->phase(), q->unipolar(), q->slew_ms());
          r->set_ok (ok); if (! ok) r->set_error ("invalid modulation target"); return Status::OK; }
        Status RemoveModulation (ServerContext*, const pb::ModTarget* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveModulation (js (q->target()));
          r->set_ok (ok); if (! ok) r->set_error ("modulation not found"); return Status::OK; }
        Status ListModulations (ServerContext*, const pb::Empty*, pb::ModList* r) override
        {
            for (auto& m : main.apiListModulations())
            {
                auto* o = r->add_mods();
                o->set_target (m.target.toStdString()); o->set_rate (m.rate);
                o->set_depth (m.depth); o->set_center (m.center); o->set_shape (m.shape); o->set_sync_beats (m.syncBeats);
                o->set_phase (m.phase); o->set_unipolar (m.unipolar); o->set_slew_ms (m.slewMs);
            }
            return Status::OK;
        }

        // ---- controller mapping / MIDI-learn ----
        Status AddControllerMap (ServerContext*, const pb::ControllerMap* q, pb::Ack* r) override
        { const bool ok = main.apiAddControllerMap (js (q->source()), js (q->target()), q->lo(), q->hi());
          r->set_ok (ok); if (! ok) r->set_error ("invalid controller map"); return Status::OK; }
        Status RemoveControllerMap (ServerContext*, const pb::ControllerSource* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveControllerMap (js (q->source()));
          r->set_ok (ok); if (! ok) r->set_error ("no map for that source"); return Status::OK; }
        Status ListControllerMaps (ServerContext*, const pb::Empty*, pb::ControllerList* r) override
        {
            for (auto& m : main.apiListControllerMaps())
            { auto* o = r->add_maps(); o->set_source (m.source.toStdString()); o->set_target (m.target.toStdString());
              o->set_lo (m.lo); o->set_hi (m.hi); o->set_bypass (m.bypass); }
            return Status::OK;
        }
        Status SetController (ServerContext*, const pb::ControllerValue* q, pb::Ack* r) override
        { main.apiSetController (js (q->source()), q->value()); r->set_ok (true); return Status::OK; }
        Status SetControllerBypass (ServerContext*, const pb::ControllerBypass* q, pb::Ack* r) override
        { const bool ok = main.apiSetControllerBypass (js (q->source()), js (q->target()), q->bypass());
          r->set_ok (ok); if (! ok) r->set_error ("controller map not found"); return Status::OK; }
        Status MidiLearn (ServerContext*, const pb::LearnRequest* q, pb::Ack* r) override
        { main.apiMidiLearn (js (q->target())); r->set_ok (true); return Status::OK; }

        // ---- universal parameter model ----
        static void fillParam (pb::ParameterInfo* pi, const MainComponent::ParamDesc& d)
        {
            pi->set_id (d.id.toStdString());   pi->set_name (d.name.toStdString());
            pi->set_value (d.value);           pi->set_min (d.min);   pi->set_max (d.max);   pi->set_default_value (d.def);
            pi->set_unit (d.unit.toStdString()); pi->set_scaling (d.scaling.toStdString());
        }
        Status ListParameters (ServerContext*, const pb::Empty*, pb::ParameterList* r) override
        {
            for (auto& d : main.apiListParameters()) fillParam (r->add_params(), d);
            return Status::OK;
        }
        Status GetParameter (ServerContext*, const pb::ParameterId* q, pb::ParameterInfo* r) override
        {
            MainComponent::ParamDesc d;
            if (! main.apiGetParameter (js (q->id()), d))
                return Status (grpc::StatusCode::NOT_FOUND, "unknown parameter id");
            fillParam (r, d);
            return Status::OK;
        }
        Status SetParameterNormalized (ServerContext*, const pb::ParameterSet* q, pb::Ack* r) override
        { const bool ok = main.apiSetParameterNormalized (js (q->id()), q->value());
          r->set_ok (ok); if (! ok) r->set_error ("unknown parameter id"); return Status::OK; }
        Status SetParameter (ServerContext*, const pb::ParameterSet* q, pb::Ack* r) override
        {
            const bool ok = main.apiSetParameter (js (q->id()), q->value());
            r->set_ok (ok); if (! ok) r->set_error ("unknown or rejected parameter id");
            return Status::OK;
        }

        // ---- diagnostics ----
        Status GetDiagnostics (ServerContext*, const pb::Empty*, pb::Diagnostics* r) override
        {
            auto d = main.apiGetDiagnostics();
            r->set_sample_rate (d.sampleRate); r->set_block_size (d.blockSize);
            r->set_inputs (d.inputs); r->set_outputs (d.outputs);
            r->set_callback_us (d.callbackUs); r->set_max_callback_us (d.maxCallbackUs);
            r->set_dsp_load (d.dspLoad); r->set_dropouts (d.dropouts); r->set_render_speed_x (d.renderSpeedX);
            return Status::OK;
        }

        Status SetProjectNotes (ServerContext*, const pb::TextValue* q, pb::Ack* r) override
        { main.apiSetProjectNotes (js (q->text())); r->set_ok (true); return Status::OK; }
        Status GetProjectNotes (ServerContext*, const pb::Empty*, pb::TextValue* r) override
        { r->set_text (main.apiGetProjectNotes().toStdString()); return Status::OK; }

        // ---- project / state ----
        Status GetState (ServerContext*, const pb::Empty*, pb::ProjectState* r) override
        {
            auto ts = main.apiGetTransport();
            auto* t = r->mutable_transport();
            t->set_playing (ts.playing); t->set_bpm (ts.bpm); t->set_position_beats (ts.positionBeats);
            for (auto& tr : main.apiListTracks())
            {
                auto* ti = r->add_tracks();
                ti->set_id (tr.id); ti->set_name (tr.name.toStdString()); ti->set_type (tr.type.toStdString());
                ti->set_volume (tr.volume); ti->set_pan (tr.pan); ti->set_mute (tr.mute); ti->set_clips (tr.clips);
                ti->set_colour (tr.colour.toStdString());
                ti->set_polarity (tr.polarity);
            }
            for (auto& ins : main.apiListInserts()) fillInsert (r->add_inserts(), ins);
            return Status::OK;
        }

        Status NewProject (ServerContext*, const pb::Empty*, pb::Ack* r) override
        { main.apiNewProject(); r->set_ok (true); return Status::OK; }

        Status ListTemplates (ServerContext*, const pb::Empty*, pb::TemplateList* r) override
        { for (auto& n : main.apiListTemplates()) r->add_names (n.toStdString()); return Status::OK; }

        Status NewFromTemplate (ServerContext*, const pb::TemplateRef* q, pb::Ack* r) override
        { const bool ok = main.apiNewFromTemplate (js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("unknown template"); return Status::OK; }

        Status SaveAsTemplate (ServerContext*, const pb::TemplateRef* q, pb::Ack* r) override
        { const bool ok = main.apiSaveAsTemplate (js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("could not save template"); return Status::OK; }

        Status Undo (ServerContext*, const pb::Empty*, pb::Ack* r) override
        { main.apiUndo(); r->set_ok (true); return Status::OK; }

        Status Redo (ServerContext*, const pb::Empty*, pb::Ack* r) override
        { main.apiRedo(); r->set_ok (true); return Status::OK; }

        Status LoadProject (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        {
            const bool ok = main.apiLoadProject (js (q->path()));
            r->set_ok (ok); if (! ok) r->set_error ("file not found");
            return Status::OK;
        }

        Status SaveProject (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        { r->set_ok (main.apiSaveProject (js (q->path()))); return Status::OK; }

        Status SaveComposition (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        { const bool ok = main.apiSaveComposition (js (q->path()));
          r->set_ok (ok); if (! ok) r->set_error ("save failed"); return Status::OK; }

        Status LoadComposition (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        { const bool ok = main.apiLoadComposition (js (q->path()));
          r->set_ok (ok); if (! ok) r->set_error ("no gloopy.toml / load failed"); return Status::OK; }

        Status RenderToFile (ServerContext*, const pb::RenderRequest* q, pb::RenderResult* r) override
        {
            double s = q->start_beat(), e = q->end_beat();
            if (! q->range_name().empty() && ! main.apiResolveRange (js (q->range_name()), s, e))
            { r->set_ok (false); r->set_error ("unknown range name"); return Status::OK; }
            const bool ok = main.apiRenderToFile (js (q->path()), q->tail_seconds(),
                                                  s, e, q->has_track_id(), q->track_id());
            r->set_ok (ok); if (! ok) { r->set_error ("render failed"); return Status::OK; }

            if (q->report())   // analyse the file we just wrote (mirror the render's extension rule)
            {
                juce::File out = juce::File::isAbsolutePath (js (q->path())) ? juce::File (js (q->path()))
                                   : juce::File::getCurrentWorkingDirectory().getChildFile (js (q->path()));
                if (! out.hasFileExtension ("wav") && ! out.hasFileExtension ("flac"))
                    out = out.withFileExtension ("wav");
                MainComponent::LoudnessReport rep;
                if (main.apiAnalyzeFile (out.getFullPathName(), rep))
                {
                    auto* lr = r->mutable_report();
                    lr->set_peak_dbfs (rep.peakDbfs); lr->set_true_peak_dbtp (rep.truePeakDbtp);
                    lr->set_rms_dbfs (rep.rmsDbfs);   lr->set_lufs (rep.lufs);
                    lr->set_momentary_lufs (rep.momentaryLufs); lr->set_short_term_lufs (rep.shortTermLufs); lr->set_lra (rep.lra);
                }
            }
            return Status::OK;
        }
        Status GetWaveform (ServerContext*, const pb::WaveformRequest* q, pb::WaveformData* r) override
        {
            std::vector<float> mins, maxs; double dur = 0.0;
            if (! main.apiGetWaveform (js (q->path()), q->buckets(), mins, maxs, dur))
                return Status (grpc::StatusCode::NOT_FOUND, "unreadable audio file");
            for (float v : mins) r->add_mins (v);
            for (float v : maxs) r->add_maxs (v);
            r->set_buckets ((int) mins.size()); r->set_duration_seconds (dur);
            return Status::OK;
        }
        Status ExportMidi (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        { const bool ok = main.apiExportMidi (js (q->path()));
          r->set_ok (ok); if (! ok) r->set_error ("midi export failed"); return Status::OK; }
        Status ExportLoopRegion (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        { const bool ok = main.apiExportLoopRegion (js (q->path()));
          r->set_ok (ok); if (! ok) r->set_error ("loop export failed (no loop set or empty)"); return Status::OK; }
        Status ExportTrack (ServerContext*, const pb::ExportTrackRequest* q, pb::Ack* r) override
        { const bool ok = main.apiExportTrack (q->track_id(), js (q->path()));
          r->set_ok (ok); if (! ok) r->set_error ("track export failed (track not found)"); return Status::OK; }
        Status ExportStems (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        { const bool ok = ! main.apiExportStems (js (q->path())).empty();
          r->set_ok (ok); if (! ok) r->set_error ("stem export failed (no instrument tracks?)"); return Status::OK; }
        Status ImportMidi (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        { const int n = main.apiImportMidi (js (q->path()));
          r->set_ok (n >= 0); if (n < 0) r->set_error ("midi import failed (unreadable or not a MIDI file)"); return Status::OK; }
        Status ImportAudio (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        { const int n = main.apiImportAudio (js (q->path()));
          r->set_ok (n >= 0); if (n < 0) r->set_error ("audio import failed (unreadable or unsupported audio file)"); return Status::OK; }
        Status AnalyzeFile (ServerContext*, const pb::FilePath* q, pb::LoudnessReport* r) override
        {
            MainComponent::LoudnessReport rep;
            if (! main.apiAnalyzeFile (js (q->path()), rep))
                return Status (grpc::StatusCode::NOT_FOUND, "unreadable audio file");
            r->set_peak_dbfs (rep.peakDbfs); r->set_true_peak_dbtp (rep.truePeakDbtp);
            r->set_rms_dbfs (rep.rmsDbfs);   r->set_lufs (rep.lufs);
            r->set_momentary_lufs (rep.momentaryLufs); r->set_short_term_lufs (rep.shortTermLufs); r->set_lra (rep.lra);
            return Status::OK;
        }

        // ---- timeline locations ----
        Status AddLocation (ServerContext*, const pb::TimelineLocation* q, pb::Ack* r) override
        { const bool ok = main.apiAddLocation (js (q->name()), js (q->kind()), q->start_beat(), q->end_beat());
          r->set_ok (ok); if (! ok) r->set_error ("invalid location"); return Status::OK; }
        Status ListLocations (ServerContext*, const pb::Empty*, pb::LocationList* r) override
        {
            for (auto& l : main.apiListLocations())
            {
                auto* o = r->add_locations();
                o->set_name (l.name.toStdString());   o->set_kind (l.kind.toStdString());
                o->set_start_beat (l.startBeat);       o->set_end_beat (l.endBeat);
            }
            return Status::OK;
        }
        Status RemoveLocation (ServerContext*, const pb::LocationName* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveLocation (js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("location not found"); return Status::OK; }

        // ---- export profiles ----
        Status DefineExportProfile (ServerContext*, const pb::ExportProfile* q, pb::Ack* r) override
        { const bool ok = main.apiDefineExportProfile (js (q->name()), js (q->target()), js (q->range_name()),
                                                       js (q->format()), q->track_id(), q->tail_seconds());
          r->set_ok (ok); if (! ok) r->set_error ("invalid export profile"); return Status::OK; }
        Status ListExportProfiles (ServerContext*, const pb::Empty*, pb::ExportProfileList* r) override
        {
            for (auto& p : main.apiListExportProfiles())
            {
                auto* o = r->add_profiles();
                o->set_name (p.name.toStdString());   o->set_target (p.target.toStdString());
                o->set_range_name (p.rangeName.toStdString()); o->set_format (p.format.toStdString());
                o->set_track_id (p.trackId);          o->set_tail_seconds (p.tailSeconds);
            }
            return Status::OK;
        }
        Status RemoveExportProfile (ServerContext*, const pb::ExportName* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveExportProfile (js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("export profile not found"); return Status::OK; }
        Status RunExport (ServerContext*, const pb::ExportRun* q, pb::ExportResult* r) override
        {
            std::vector<juce::String> files;
            const bool ok = main.apiRunExport (js (q->name()), js (q->out_dir()), files);
            r->set_ok (ok);
            if (ok) for (auto& f : files) r->add_files (f.toStdString());
            else    r->set_error ("unknown profile, unresolved range, or render failed");
            return Status::OK;
        }

        // ---- track & clip management ----
        Status RemoveTrack (ServerContext*, const pb::TrackId* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveTrack (q->id()); r->set_ok (ok); if (! ok) r->set_error ("track not found"); return Status::OK; }

        Status RenameTrack (ServerContext*, const pb::RenameTrackRequest* q, pb::Ack* r) override
        { const bool ok = main.apiRenameTrack (q->track_id(), js (q->name()));
          r->set_ok (ok); if (! ok) r->set_error ("rename failed (track not found or empty name)"); return Status::OK; }

        Status SetTrackColour (ServerContext*, const pb::SetTrackColourRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetTrackColour (q->track_id(), js (q->colour()));
          r->set_ok (ok); if (! ok) r->set_error ("recolour failed (track not found)"); return Status::OK; }

        Status MoveTrack (ServerContext*, const pb::MoveTrackRequest* q, pb::Ack* r) override
        { const bool ok = main.apiMoveTrack (q->track_id(), q->delta());
          r->set_ok (ok); if (! ok) r->set_error ("move failed (track not found or already at edge)"); return Status::OK; }

        Status SetTrackPolarity (ServerContext*, const pb::SetTrackPolarityRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetTrackPolarity (q->track_id(), q->invert());
          r->set_ok (ok); if (! ok) r->set_error ("polarity failed (track not found)"); return Status::OK; }

        Status AddAudioTrack (ServerContext*, const pb::AddAudioTrackRequest* q, pb::TrackId* r) override
        { r->set_id (main.apiAddAudioTrack (js (q->name()))); return Status::OK; }

        Status AddSamplerTrack (ServerContext*, const pb::AddSamplerTrackRequest* q, pb::TrackId* r) override
        { r->set_id (main.apiAddSamplerTrack (js (q->name()), js (q->path()), q->root_note())); return Status::OK; }

        Status SetSamplerControls (ServerContext*, const pb::SamplerControlsRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetSamplerControls (q->track_id(), q->start(), q->end(), q->reverse(), q->root_note(), q->fade_in(), q->fade_out(), q->loop(), q->mono());
          r->set_ok (ok); if (! ok) r->set_error ("not a sampler track"); return Status::OK; }

        Status GetSamplerControls (ServerContext*, const pb::TrackId* q, pb::SamplerControls* r) override
        { const auto s = main.apiGetSamplerControls (q->id());
          r->set_ok (s.ok); r->set_start (s.start); r->set_end (s.end); r->set_reverse (s.reverse);
          r->set_root_note (s.rootNote); r->set_name (s.name.toStdString());
          r->set_fade_in (s.fadeIn); r->set_fade_out (s.fadeOut); r->set_loop (s.loop); r->set_mono (s.mono); return Status::OK; }

        Status AddSfzTrack (ServerContext*, const pb::AddSfzTrackRequest* q, pb::TrackId* r) override
        { r->set_id (main.apiAddSfzTrack (js (q->name()), js (q->path()))); return Status::OK; }

        Status AddPluginTrack (ServerContext*, const pb::AddPluginTrackRequest* q, pb::TrackId* r) override
        { r->set_id (main.apiAddPluginTrack (js (q->identifier()))); return Status::OK; }

        Status RemoveClip (ServerContext*, const pb::ClipRef* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveClip (q->track_id(), q->index()); r->set_ok (ok); if (! ok) r->set_error ("clip not found"); return Status::OK; }

        Status MoveClip (ServerContext*, const pb::MoveClipRequest* q, pb::Ack* r) override
        {
            const bool ok = main.apiMoveClip (q->track_id(), q->index(), q->start_beat(),
                                              q->has_to_track_id(), q->to_track_id());
            r->set_ok (ok); if (! ok) r->set_error ("clip not found");
            return Status::OK;
        }

        Status AddAudioClip (ServerContext*, const pb::AddAudioClipRequest* q, pb::ClipId* r) override
        {
            const int idx = main.apiAddAudioClip (q->track_id(), q->start_beat(), js (q->path()), q->gain());
            r->set_track_id (q->track_id()); r->set_index (idx);
            return Status::OK;
        }

        // ---- clip / region operations ----
        Status SplitClip (ServerContext*, const pb::SplitClipRequest* q, pb::ClipId* r) override
        {
            const int idx = main.apiSplitClip (q->track_id(), q->index(), q->beat());
            r->set_track_id (q->track_id()); r->set_index (idx);
            return Status::OK;
        }
        Status SplitClipAtMarker (ServerContext*, const pb::SplitAtMarkerRequest* q, pb::ClipId* r) override
        {
            const int idx = main.apiSplitClipAtMarker (q->track_id(), q->index(), js (q->marker()));
            r->set_track_id (q->track_id()); r->set_index (idx);
            return Status::OK;
        }
        Status SliceAtTransients (ServerContext*, const pb::SliceTransientsRequest* q, pb::SliceResult* r) override
        { r->set_slices (main.apiSliceClipAtTransients (q->track_id(), q->index(), q->sensitivity())); return Status::OK; }
        Status SetClipMuted (ServerContext*, const pb::ClipMuteRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetClipMuted (q->track_id(), q->index(), q->muted());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found"); return Status::OK; }
        Status SetLoopToClip (ServerContext*, const pb::ClipRef* q, pb::Ack* r) override
        { const bool ok = main.apiSetLoopToClip (q->track_id(), q->index());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found"); return Status::OK; }
        Status SetMetronome (ServerContext*, const pb::MetronomeRequest* q, pb::Ack* r) override
        { main.apiSetMetronome (q->enabled()); r->set_ok (true); return Status::OK; }
        Status SetMetronomeLevel (ServerContext*, const pb::MetronomeLevel* q, pb::Ack* r) override
        { main.apiSetMetronomeLevel (q->level()); r->set_ok (true); return Status::OK; }
        Status GetMetronomeLevel (ServerContext*, const pb::Empty*, pb::MetronomeLevel* r) override
        { r->set_level (main.apiGetMetronomeLevel()); return Status::OK; }
        Status DuplicateClip (ServerContext*, const pb::DuplicateClipRequest* q, pb::ClipId* r) override
        {
            const int idx = main.apiDuplicateClip (q->track_id(), q->index(), q->at_beat());
            r->set_track_id (q->track_id()); r->set_index (idx);
            return Status::OK;
        }
        Status RepeatClip (ServerContext*, const pb::RepeatClipRequest* q, pb::SliceResult* r) override
        { r->set_slices (main.apiRepeatClip (q->track_id(), q->index(), q->copies())); return Status::OK; }
        Status ReverseClip (ServerContext*, const pb::ClipRef* q, pb::Ack* r) override
        { const bool ok = main.apiReverseClip (q->track_id(), q->index());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found"); return Status::OK; }
        Status CropClip (ServerContext*, const pb::CropClipRequest* q, pb::Ack* r) override
        { const bool ok = main.apiCropClip (q->track_id(), q->index(), q->start_beat(), q->end_beat());
          r->set_ok (ok); if (! ok) r->set_error ("crop failed (clip not found, not MIDI, or empty range)"); return Status::OK; }
        Status ScaleClipTime (ServerContext*, const pb::ScaleTimeRequest* q, pb::Ack* r) override
        { // proto3 omits an unset factor (0.0); no-op scaling is meaningless, read it as double-time.
          const double f = q->factor() <= 0.0 ? 0.5 : q->factor();
          const bool ok = main.apiScaleClipTime (q->track_id(), q->index(), f);
          r->set_ok (ok); if (! ok) r->set_error ("scale time failed (clip not found or not MIDI)"); return Status::OK; }
        Status ConsolidateClip (ServerContext*, const pb::ClipRef* q, pb::Ack* r) override
        { const bool ok = main.apiConsolidateClip (q->track_id(), q->index());
          r->set_ok (ok); if (! ok) r->set_error ("consolidate failed (clip not found or not MIDI)"); return Status::OK; }
        Status BounceClip (ServerContext*, const pb::ClipRef* q, pb::TrackId* r) override
        { r->set_id (main.apiBounceClip (q->track_id(), q->index())); return Status::OK; }
        Status SetClipGain (ServerContext*, const pb::ClipGainRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetClipGain (q->track_id(), q->index(), q->gain_db());
          r->set_ok (ok); if (! ok) r->set_error ("set clip gain failed (clip not found or not audio)"); return Status::OK; }
        Status NormalizeClip (ServerContext*, const pb::NormalizeClipRequest* q, pb::Ack* r) override
        { const bool ok = main.apiNormalizeClip (q->track_id(), q->index(), q->target_dbfs()) >= 0.0f;
          r->set_ok (ok); if (! ok) r->set_error ("normalize failed (clip not found, not audio, or silent)"); return Status::OK; }
        Status SetClipFades (ServerContext*, const pb::ClipFadesRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetClipFades (q->track_id(), q->index(), q->fade_in_beats(), q->fade_out_beats());
          r->set_ok (ok); if (! ok) r->set_error ("set clip fades failed (clip not found or not audio)"); return Status::OK; }
        Status SetClipFadeShape (ServerContext*, const pb::ClipFadeShapeRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetClipFadeShape (q->track_id(), q->index(), q->shape());
          r->set_ok (ok); if (! ok) r->set_error ("set clip fade shape failed (clip not found or not audio)"); return Status::OK; }
        Status GetClipNotes (ServerContext*, const pb::ClipRef* q, pb::NoteList* r) override
        {
            for (auto& n : main.apiGetClipNotes (q->track_id(), q->index()))
            {
                auto* o = r->add_notes();
                o->set_pitch (n.pitch); o->set_start_beat (n.startBeat);
                o->set_length_beats (n.lengthBeats); o->set_velocity (n.velocity);
                o->set_probability (n.probability);
            }
            return Status::OK;
        }
        Status ExportNotesJSON (ServerContext*, const pb::ClipRef* q, pb::NotesJson* r) override
        { r->set_json (main.apiExportClipNotesJson (q->track_id(), q->index()).toStdString()); return Status::OK; }
        Status ImportNotesJSON (ServerContext*, const pb::ImportNotesRequest* q, pb::ClipId* r) override
        { const int idx = main.apiImportClipNotesJson (q->track_id(), q->start_beat(), js (q->json()));
          r->set_track_id (q->track_id()); r->set_index (idx); return Status::OK; }
        Status SetClipTranspose (ServerContext*, const pb::ClipTransposeRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetClipTranspose (q->track_id(), q->index(), q->semitones());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found or not MIDI"); return Status::OK; }
        Status SetClipVelocity (ServerContext*, const pb::ClipVelocityRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSetClipVelocity (q->track_id(), q->index(), q->scale());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found or not MIDI"); return Status::OK; }
        Status SetClipProbability (ServerContext*, const pb::ClipVelocityRequest* q, pb::Ack* r) override   // scale = probability
        { const bool ok = main.apiSetClipProbability (q->track_id(), q->index(), q->scale());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found or not MIDI"); return Status::OK; }
        Status QuantizeClip (ServerContext*, const pb::QuantizeRequest* q, pb::Ack* r) override
        { const double strength = q->strength() <= 0.0 ? 1.0 : q->strength();   // proto3 omits 0; default to full
          const bool ok = main.apiQuantizeClip (q->track_id(), q->index(), q->grid(), strength);
          r->set_ok (ok); if (! ok) r->set_error ("clip not found or not MIDI"); return Status::OK; }
        Status TransposeClip (ServerContext*, const pb::TransposeRequest* q, pb::Ack* r) override
        { const bool ok = main.apiTransposeClip (q->track_id(), q->index(), q->semitones());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found or not MIDI"); return Status::OK; }
        Status HumanizeClip (ServerContext*, const pb::HumanizeRequest* q, pb::Ack* r) override
        { const bool ok = main.apiHumanizeClip (q->track_id(), q->index(), q->timing(), q->velocity());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found or not MIDI"); return Status::OK; }
        Status AddChord (ServerContext*, const pb::ChordRequest* q, pb::Ack* r) override
        { const bool ok = main.apiAddChord (q->track_id(), q->index(), q->root(), js (q->type()),
                                            q->start_beat(), q->length_beats(), q->velocity(), q->inversion());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found or not MIDI"); return Status::OK; }
        Status LegatoClip (ServerContext*, const pb::LegatoRequest* q, pb::Ack* r) override
        { // proto3 omits an unset amount (0.0), which would be a no-op; read it as full legato.
          const float amt = q->amount() <= 0.0f ? 1.0f : q->amount();
          const bool ok = main.apiLegatoClip (q->track_id(), q->index(), amt);
          r->set_ok (ok); if (! ok) r->set_error ("legato failed (clip not found or not MIDI)"); return Status::OK; }
        Status RampClipVelocity (ServerContext*, const pb::VelRampRequest* q, pb::Ack* r) override
        { const bool ok = main.apiRampClipVelocity (q->track_id(), q->index(), q->from(), q->to());
          r->set_ok (ok); if (! ok) r->set_error ("velocity ramp failed (clip not found or not MIDI)"); return Status::OK; }
        Status EchoClip (ServerContext*, const pb::EchoRequest* q, pb::Ack* r) override
        { // proto3 omits unset repeats(0)/feedback(0); default to a musical 3 echoes at 0.6 feedback.
          const int reps = q->repeats() <= 0 ? 3 : q->repeats();
          const float fb = q->feedback() <= 0.0f ? 0.6f : q->feedback();
          const bool ok = main.apiEchoClip (q->track_id(), q->index(), q->delay_beats(), reps, fb);
          r->set_ok (ok); if (! ok) r->set_error ("echo failed (clip not found or not MIDI)"); return Status::OK; }
        Status InvertClip (ServerContext*, const pb::ClipRef* q, pb::Ack* r) override
        { const bool ok = main.apiInvertClip (q->track_id(), q->index());
          r->set_ok (ok); if (! ok) r->set_error ("invert failed (clip not found or not MIDI)"); return Status::OK; }
        Status RatchetClip (ServerContext*, const pb::RatchetRequest* q, pb::Ack* r) override
        { const int sub = q->subdivisions() <= 1 ? 2 : q->subdivisions();   // proto3 omits 0; default to x2
          const bool ok = main.apiRatchetClip (q->track_id(), q->index(), sub);
          r->set_ok (ok); if (! ok) r->set_error ("ratchet failed (clip not found or not MIDI)"); return Status::OK; }
        Status HarmonizeClip (ServerContext*, const pb::HarmonizeRequest* q, pb::Ack* r) override
        { const bool ok = main.apiHarmonizeClip (q->track_id(), q->index(), q->semitones());
          r->set_ok (ok); if (! ok) r->set_error ("harmonize failed (clip not found or not MIDI)"); return Status::OK; }
        Status SwingClip (ServerContext*, const pb::SwingClipRequest* q, pb::Ack* r) override
        { const double grid = q->grid_beats() <= 0.0 ? 0.5 : q->grid_beats();   // proto3 omits 0; default to 1/8
          const bool ok = main.apiSwingClip (q->track_id(), q->index(), grid, q->amount());
          r->set_ok (ok); if (! ok) r->set_error ("swing failed (clip not found or not MIDI)"); return Status::OK; }
        Status ChordifyClip (ServerContext*, const pb::ChordifyRequest* q, pb::Ack* r) override
        { const bool ok = main.apiChordifyClip (q->track_id(), q->index(), q->chord_type());   // 0 (major) is the proto3 default
          r->set_ok (ok); if (! ok) r->set_error ("chordify failed (clip not found or not MIDI)"); return Status::OK; }
        Status StrumClip (ServerContext*, const pb::StrumRequest* q, pb::Ack* r) override
        { const bool ok = main.apiStrumClip (q->track_id(), q->index(), q->step_beats(), q->down());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found or not MIDI"); return Status::OK; }
        Status ArpeggiateClip (ServerContext*, const pb::ArpeggiateRequest* q, pb::Ack* r) override
        { const bool ok = main.apiArpeggiateClip (q->track_id(), q->index(), q->step_beats(), q->mode());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found or not MIDI"); return Status::OK; }
        Status SplitNotesAtBeat (ServerContext*, const pb::SplitNotesRequest* q, pb::Ack* r) override
        { const bool ok = main.apiSplitNotesAtBeat (q->track_id(), q->index(), q->beat());
          r->set_ok (ok); if (! ok) r->set_error ("clip not found or not MIDI"); return Status::OK; }
        Status SetTrackArp (ServerContext*, const pb::ArpSpec* q, pb::Ack* r) override
        { // proto3 omits an unset probability (0.0); a 0% arp is meaningless, so read it as full (1.0).
          const float prob = q->probability() <= 0.0f ? 1.0f : q->probability();
          const bool ok = main.apiSetTrackArp (q->track_id(), q->enabled(), q->rate(), q->octaves(), q->gate(), q->mode(), q->swing(), q->hold(), prob);
          r->set_ok (ok); if (! ok) r->set_error ("track not found"); return Status::OK; }
        Status GetTrackArp (ServerContext*, const pb::TrackRef2* q, pb::ArpSpec* r) override
        { bool en=false, hold=false; double rate=0.25; int oct=1, mode=0; float gate=0.5f, swing=0.0f, prob=1.0f;
          if (main.apiGetTrackArp (q->track_id(), en, rate, oct, gate, mode, swing, hold, prob))
          { r->set_track_id (q->track_id()); r->set_enabled (en); r->set_rate (rate);
            r->set_octaves (oct); r->set_gate (gate); r->set_mode (mode); r->set_swing (swing); r->set_hold (hold);
            r->set_probability (prob); }
          return Status::OK; }

        // ---- plugins ----
        Status ScanPlugins (ServerContext*, const pb::ScanPluginsRequest* q, pb::PluginList* r) override
        { for (auto& p : main.apiScanPlugins (q->force())) fillPlugin (r->add_plugins(), p); return Status::OK; }

        Status ListPlugins (ServerContext*, const pb::Empty*, pb::PluginList* r) override
        { for (auto& p : main.apiListPlugins()) fillPlugin (r->add_plugins(), p); return Status::OK; }

        Status AddPluginEffect (ServerContext*, const pb::AddPluginEffectRequest* q, pb::EffectRef* r) override
        {
            const int slot = main.apiAddPluginEffect (q->insert(), js (q->identifier()));
            r->set_insert (q->insert()); r->set_slot (slot);
            return Status::OK;
        }

        Status OpenPluginEditor (ServerContext*, const pb::TrackId* q, pb::Ack* r) override
        { const bool ok = main.apiOpenPluginEditor (q->id()); r->set_ok (ok); if (! ok) r->set_error ("no plugin on track"); return Status::OK; }

        // ---- events (streaming out) ----
        Status Subscribe (ServerContext* ctx, const pb::SubscribeRequest* q, ServerWriter<pb::Event>* writer) override
        {
            const int interval = q->interval_ms() > 0 ? (int) q->interval_ms() : 50;
            const int sinkId = q->changes() ? main.apiAddChangeSink() : -1;
            while (! ctx->IsCancelled())
            {
                if (sinkId >= 0)
                {
                    std::vector<MainComponent::ChangeSnap> changes;
                    main.apiPollChanges (sinkId, changes);
                    for (auto& c : changes)
                    {
                        pb::Event e;
                        auto* ch = e.mutable_change();
                        ch->set_kind (c.kind.toStdString()); ch->set_track_id (c.trackId); ch->set_insert (c.insert);
                        if (! writer->Write (e)) { main.apiRemoveChangeSink (sinkId); return Status::OK; }
                    }
                }
                if (q->transport())
                {
                    auto s = main.apiGetTransport();
                    pb::Event e;
                    auto* t = e.mutable_transport();
                    t->set_playing (s.playing); t->set_bpm (s.bpm); t->set_position_beats (s.positionBeats);
                    if (! writer->Write (e)) break;
                }
                if (q->meters())
                {
                    std::vector<float> L, R; std::vector<char> clip;
                    if (main.apiSnapshotMeters (L, R, clip))
                    {
                        pb::Event e;
                        auto* m = e.mutable_meters();
                        for (float v : L) m->add_peak_l (v);
                        for (float v : R) m->add_peak_r (v);
                        for (char c : clip) m->add_clipped (c != 0);
                        if (! writer->Write (e)) break;
                    }
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (interval));
            }
            if (sinkId >= 0) main.apiRemoveChangeSink (sinkId);
            return Status::OK;
        }

    private:
        MainComponent& main;
    };
}

struct GrpcServer::Impl
{
    explicit Impl (MainComponent& mc) : service (mc) {}
    ServiceImpl service;
    std::unique_ptr<Server> server;
    std::thread thread;
};

GrpcServer::GrpcServer (MainComponent& owner) : impl (std::make_unique<Impl> (owner)) {}
GrpcServer::~GrpcServer() { stop(); }

bool GrpcServer::start (int port)
{
    ServerBuilder builder;
    const std::string addr = "127.0.0.1:" + std::to_string (port);
    builder.AddListeningPort (addr, grpc::InsecureServerCredentials());
    builder.RegisterService (&impl->service);
    impl->server = builder.BuildAndStart();
    if (impl->server == nullptr)
        return false;
    impl->thread = std::thread ([this] { impl->server->Wait(); });
    return true;
}

void GrpcServer::stop()
{
    if (impl != nullptr && impl->server != nullptr)
    {
        impl->server->Shutdown();
        if (impl->thread.joinable()) impl->thread.join();
        impl->server.reset();
    }
}
