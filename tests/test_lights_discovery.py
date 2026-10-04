"""Exercise the actual HA discovery templates without needing a running HA."""
import json
import unittest
from pathlib import Path
from types import SimpleNamespace as NS

import jinja2
import yaml

ROOT = Path(__file__).resolve().parents[1]
AUTOMATION = yaml.safe_load((ROOT / 'homeassistant/whybadge-lights-automation.yaml').read_text())
ENV = jinja2.Environment(undefined=jinja2.StrictUndefined)
ENV.filters['to_json'] = json.dumps


def light(entity='light.desk', state='on', **attrs):
    return NS(entity_id=entity, state=state, attributes=attrs)


def catalog(lights):
    template = AUTOMATION['actions'][1]['data']['payload']
    wire = ENV.from_string(template).render(
        states=NS(light=lights), area_name=lambda entity: 'Study' if entity == 'light.desk' else None).strip()
    assert wire.startswith('WHY1:')  # Remains a string in HA's native template renderer.
    return json.loads(wire[5:])


class DiscoveryTests(unittest.TestCase):
    def test_names_escaping_brightness_and_capabilities(self):
        result = catalog([
            light(friendly_name='Desk "lamp" \\ light', brightness=128, supported_color_modes=['rgb']),
            light('light.relay', 'off', supported_color_modes=['onoff']),
            light('light.missing', 'unavailable', brightness=None, supported_color_modes=None),
        ])
        desk = next(x for x in result['lights'] if x['id'] == 'light.desk')
        self.assertEqual(desk['name'], 'Desk "lamp" \\ light')
        self.assertEqual(desk['area'], 'Study')
        self.assertEqual(desk['b'], 50)
        self.assertTrue(desk['dim'])
        self.assertFalse(next(x for x in result['lights'] if x['id'] == 'light.relay')['dim'])
        missing = next(x for x in result['lights'] if x['id'] == 'light.missing')
        self.assertEqual(missing['state'], 'unavailable')
        self.assertEqual(missing['b'], 0)
        self.assertFalse(missing['dim'])

    def test_empty_catalog(self):
        self.assertEqual(catalog([]), {'lights': [], 'total': 0})

    def test_bounded_large_catalog_and_names(self):
        result = catalog([light(f'light.test_{i:03}', friendly_name='é' * 100,
                                supported_color_modes=['brightness']) for i in range(200)])
        self.assertEqual(result['total'], 200)
        self.assertGreater(len(result['lights']), 0)
        self.assertLessEqual(len(result['lights']), 128)
        self.assertEqual(len(result['lights'][0]['name']), 60)
        # HA's to_json uses UTF-8, unlike json.dumps's default ASCII escapes.
        self.assertLess(len(json.dumps(result).encode()), 30500)

    def test_only_light_changes_trigger_event_refresh(self):
        template = ENV.from_string(AUTOMATION['conditions'][0]['value_template'])
        def accepts(trigger):
            return template.render(trigger=trigger).strip() == 'True'
        self.assertTrue(accepts(NS(platform='time_pattern')))
        self.assertTrue(accepts(NS(platform='event', event=NS(event_type='esphome.whybadge_lights_discover'))))
        self.assertTrue(accepts(NS(platform='event', event=NS(event_type='state_changed', data=NS(entity_id='light.desk')))))
        self.assertFalse(accepts(NS(platform='event', event=NS(event_type='state_changed', data=NS(entity_id='sensor.temp')))))


if __name__ == '__main__':
    unittest.main()
