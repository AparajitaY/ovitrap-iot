# How the Smart Ovitrap Works

This document explains the sensing logic, biological reasoning, and
decision-making behind the prototype.

## Target Mosquito

The system is designed around **Aedes aegypti**, the primary urban vector
for dengue, chikungunya, and Zika. It breeds in small, stagnant water
containers—exactly the environment an ovitrap mimics—and is most active
during dawn and dusk.

By monitoring activity at the trap entrance rather than relying on manual
inspection, the device enables continuous, low-cost surveillance in
dense urban settings like Vellore.

## Why Dual IR Sensors?

Two infrared beam-break sensors are mounted 20 mm apart at the entry
aperture. As an insect flies through, it interrupts both beams in
sequence.

The ESP32 measures the time between the two interruptions and accepts
only events falling within a **40–133 ms** window, corresponding to the
natural flight speed of Aedes aegypti (15–50 cm/s).

This "speed gating" filters out:
- Larger insects moving too slowly or too fast
- Dust, debris, or sensor noise
- False triggers from ambient disturbances

No camera, microphone, or AI model is required—keeping the system
low-cost, low-power, and suitable for battery-powered field deployment.

## What Is Crepuscular Cycling?

"Crepuscular" describes animals most active during twilight—dawn and
dusk. Aedes aegypti bites and breeds predominantly in these windows.

The device synchronizes its clock via NTP and applies a **1.8×
multiplier** to entry events recorded during dawn/dusk periods. This
weights the count toward biologically meaningful activity rather than
treating all hours equally.

## Why Temperature and Humidity?

Aedes aegypti survival, breeding rate, and egg development are strongly
influenced by ambient temperature and relative humidity.

The DHT22 sensor continuously measures both. When readings fall within
ranges empirically associated with elevated breeding activity, the
firmware applies a **1.4× weather-favorability multiplier** to the
event count.

This makes the system responsive to real environmental conditions,
not just raw insect crossings.

## The Activity Index

The ESP32 combines three inputs into a single composite score:

| Input | Role |
|-------|------|
| Rolling 30-min count of validated crossings | Raw activity signal |
| Crepuscular multiplier (1.8×) | Weights dawn/dusk activity |
| Weather multiplier (1.4×) | Weights favorable breeding conditions |

The resulting index is thresholded into three tiers:

- **Low** → Green LED
- **Medium** → Yellow LED
- **High** → Red LED

This gives field workers an immediate, connectivity-independent visual
cue without needing to check a phone or dashboard.

## How It Helps

By streaming the Activity Index, environmental data, and GPS location
from multiple nodes via MQTT to a cloud dashboard, the system supports:

- **Early detection** of rising mosquito activity before outbreaks
- **Spatial comparison** across deployment sites (e.g., Vellore wards)
- **Targeted intervention**—directing fogging or larvicide to high-risk
  zones instead of blanket treatment
- **Continuous autonomous surveillance** without manual trap checks

This shifts vector control from reactive to proactive, supporting
SDG 3 (Good Health and Well-Being).

## Limitations

- Timing-based filtering does not confirm mosquito species.
- Accepted events are a proxy for activity, not a verified species count.
- The Activity Index is a prototype monitoring metric, not a validated
  disease-transmission predictor.
- Broader field testing is needed to assess accuracy, false positives,
  and outdoor reliability.
