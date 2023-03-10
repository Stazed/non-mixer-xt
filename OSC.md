# Controlling Non-Mixer-XT with OSC

For the most part, the original Non-Mixer OSC Control documentation applies to Non-Mixer-XT:

http://non.tuxfamily.org/mixer/doc/MANUAL.html#n:1.2.3.1.1.

Notable changes include the ability to query the value of any parameter (input and output) with an osc message, the abitily to bypass plugins with an osc message and the ability to query retrieve the vu-meter's level value).

## Signals

### Topology

Non-Mixer-XT exposes all controllable parameters with a generic OSC API.
Exposed parameters are refered to here as "signals".

There are two types of signals :

- **Input** signals (plugin parameters, gain and mute state, etc): can have their value set and queried with an osc message.
- **Output** signals (meter level, plugin output control such as a compressor's gain reduction, etc): can only be queried.


The OSC path of a signal is automatically generated as follow:

```
/strip/[STRIP_NAME]/[MODULE_NAME]/[PARAMETER_NAME]
```

Sending a value between `0.0` and `1.0` (integer or float) will change the signal's value,
sending a message with no value is is considered as a query and will send the signal's current value back to the sender.


An alternative path is also available, that uses exact values (integer or float) instead of normalized ones (e.g. gain would go from -70 to +6):

```
/strip/[STRIP_NAME]/[MODULE_NAME]/[PARAMETER_NAME]/unscaled
```

### Extra signals


Plugins that can be bypassed (i.e. that have the same number of audio inputs and outputs, or that have 1 input and 2 outputs) have an extra signal exposed to allow changing their bypass state with an osc message:

```
/strip/[STRIP_NAME]/[MODULE_NAME]/dsp/bypass
```

The meter level of a strip is also exposed with a dedicated output signal. Only one value is returned regardless of the number of audio outputs (the loudest channel's level is returned):

```
/strip/[STRIP_NAME]/Meter/Level%20(dB)
```

Output parameters of plugins (e.g. a compressor's gain reduction) are not shown generic plugin interfaces but their signals can be quiered, see [Signal listing](#signal-listing).


### Signals under NSM

When Non-Mixer-XT is managed by a Non Session Manager, signal paths are all prepended with `Non-Mixer-XT.clientID`:

```
Non-Mixer-XT.clientID/strip/[STRIP_NAME]/[MODULE_NAME]/[PARAMETER_NAME]
```

The absence of leading slash is not a typing error. It does not comply with the OSC specification but is supported in some implementations such as liblo. This may change in a future version.

## Commands

Non-Mixer-XT handles a few global OSC commands to help building remote controllers. These commands are not signals and thus are not affected under Non Session Managment.  

### Signal listing

`/signal/list`

For each signal, the following message will be sent back to the sender:

```
/reply ,sssfff "/signal/list" signal_path direction min max default
```

*Arguments*

- `"/signal/list"`: indicates which command the reply is for
- `signal_path`: osc path the signal
- `direction`: "in" or "out"
- `min`: minimum value
- `min`: maximum value
- `default`: default value


When all replies have been sent, an additional message is sent to indicate the reply is complete:

```
/reply ,s "/signal/list"
```

### Signal listing (partial)

```
/signal/list ,s prefix
```

Works like the previous command except only signal's whose paths start with `prefix` will be listed.

### Signal informations

```
/signal/infos ,s signal_path
```

If provided `signal_path` exists, the following message will be sent back to the sender:

```
/reply ,sssis "/signal/infos" signal_path type label
```

*Arguments*

- `"/signal/list"`: indicates which command the reply is for
- `signal_path`: indicates which signal the reply is for
- `type`: indicates which type of parameter is controlled by the signal, it's mostly useful to determine whether a parameter can be controlled by a toggle button or not:
    - `0`: linear
    - `1`: logarithmic
    - `2`: boolean
    - `3`: LV2 integer
    - `4`: LV2 integer enumeration
- `label`: for LV2 plugins, the parameter's symbol is used to generate the OSC path of the signal. This provides a human readable name for the parameter.


### Signal subscription

To enable signal subscription, one must first send the following command to Non-Mixer-XT:

```
/signal/hello ,ss name url
```

*Arguments*

- `name`: name associated with the controller
- `url`: server url to which update messages will be sent (e.g: `osc.udp://127.0.0.1:12345`)


#### Subscribe

```
/signal/connect ,ss destination_path source_path
```

Will make Non-Mixer-XT send a message whenever the signal's value changes. This works **only for input signals**.

*Arguments*

- `destination_path`: osc path used for update messages
- `source_path`: osc path of subscribe signal


#### Unsusbscribe

```
/signal/disconnect ,ss destination_path source_path
```

Will cancel the signal subscription.

*Arguments*

- `destination_path`: osc path used for update messages
- `source_path`: osc path of subscribe signal
