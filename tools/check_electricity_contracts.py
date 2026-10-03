#!/usr/bin/env python3
"""Guard the FX flag and shared color/glow geometry integration."""
from pathlib import Path
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'


class ElectricityContracts(unittest.TestCase):
    def test_offhand_pose_is_shared_and_does_not_override_npcs_or_cameras(self):
        for tree in ('code', 'codeJK2'):
            source = (ROOT / tree / 'game/bg_misc.cpp').read_text()
            pose = body(source, 'BG_CalculateVRLightningPose')
            self.assertIn('ent->s.number != 0', pose)
            self.assertIn('in_camera', pose)
            self.assertIn('!BG_UseVRPosition(ent)', pose)
            self.assertIn('ent->client->ps.viewEntity', pose)
            self.assertIn('BG_CalculateVRDefaultPosition(1, origin, angles)', pose)
            default = body(source, 'BG_CalculateVRDefaultPosition')
            self.assertIn('vr->offhandangles[ANGLES_DEFAULT]', default)
            self.assertNotIn('ANGLES_ADJUSTED', default)

    def test_visual_and_damage_rays_use_same_cast_pose(self):
        for tree in ('code', 'codeJK2'):
            game = (ROOT / tree / 'game/wp_saber.cpp').read_text()
            shoot = body(game, 'ForceShootLightning')
            self.assertIn('BG_CalculateVRLightningPose(self, castOrigin, castAngles)', shoot)
            self.assertIn('AngleVectors(castAngles, forward, NULL, NULL)', shoot)
            self.assertIn('trackedLightning ? castOrigin : self->currentOrigin, center', shoot)
            self.assertIn('gi.inPVS( ent_org, castOrigin )', shoot)
            self.assertIn('gi.trace( &tr, castOrigin,', shoot)
            self.assertIn('VectorMA( castOrigin, 2048, forward, end )', shoot)
            self.assertEqual(shoot.count('self->client->renderInfo.handLPoint'), 1)
            for file, origin, angles in (('cg_players.cpp', 'fxOrigin', 'tAng'),
                                         ('cg_weapons.cpp', 'temp', 'castAngles' if tree == 'code' else 'tAng')):
                source = (ROOT / tree / 'cgame' / file).read_text()
                self.assertIn(f'BG_CalculateVRLightningPose(cent->gent, {origin}, {angles})', source)
                self.assertIn(f'forceLightningWide, {origin}, fxAxis', source)
                self.assertNotIn('forceLightningWide, temp, cg.refdef.viewaxis', source)
                self.assertNotIn('forceLightning, temp, cg.refdef.viewaxis[0]', source)

    def test_both_games_preserve_authored_flag_aliases(self):
        for tree in ('code', 'codeJK2'):
            header = (ROOT / tree / 'cgame/FxPrimitives.h').read_text()
            self.assertRegex(header, r'#define\s+FX_BRANCH\s+0x02000000')
            self.assertRegex(header, r'#define\s+FX_APPLY_PHYSICS\s+0x02000000')
            source = (ROOT / tree / 'cgame/FxPrimitives.cpp').read_text()
            init = body(source, 'CElectricity::Initialize')
            self.assertIn('mFlags & FX_BRANCH', init)
            self.assertIn('mRefEnt.renderfx |= RF_FORKED', init)
            template = (ROOT / tree / 'cgame/FxTemplate.cpp').read_text()
            self.assertIn('"usePhysics" ), FX_APPLY_PHYSICS', template)

    def test_renderer_uses_one_immutable_generator_for_both_passes(self):
        source = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()
        build = body(source, 'VK_BuildDynamicEffectBatches')
        self.assertIn('effect.forked = ( entity.renderfx & RF_FORKED ) != 0', build)
        self.assertIn('effect.tapered = ( entity.renderfx & RF_TAPERED ) != 0', build)
        self.assertIn('entity.renderfx & RF_GROW', build)
        self.assertIn('effect.seed = entity.frame', build)
        self.assertIn('VK_BuildElectricity( effect', build)
        self.assertIn('entity.shaderRGBA, line.startUV, line.endUV', build)
        self.assertNotIn('Q_random(', build)
        self.assertNotIn('Q_crandom(', build)
        for name in ('VK_RecordDynamicEffects', 'VK_RecordDynamicGlowSources'):
            self.assertIn('VK_BuildDynamicEffectBatches(', body(source, name))


if __name__ == '__main__':
    unittest.main()
