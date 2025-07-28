This is a fork of the stock indi driver for [Quantum filter wheel](https://github.com/indilib/indi/blob/master/drivers/filter_wheel/quantum_wheel.cpp).

# Features

It extends the protocol for use in custom/home made hardware, with the
following functions:
  * a generic setting control (to adjust tolerance, motor speed, whatever
    the firmware will advertize)
  * explicit report of error when switching filter fail (by extending the
    command set)
  * optional report of precision after a successfull switch (to check your
    FW does not degrade over time)

# Compilation

```bash
cmake .
make
make install
```

You can then use the `quantum_wheel_plus` driver instead of the
`quantum_wheel` one.

# Protocol addition

The driver is fully compatible with existing hardware and will activate new features only when it detects a "+" sign in the description string (in response to the `SN` command).

## Settings

When the "+" sign is detected, the following additional commands are used by the driver:

| Description                | Command         | Response               |
|----------------------------|-----------------|------------------------|
| List settings              | `s?`            | `s<id1><id2>...`       |
| Get setting                | `s<id>`         | `s<id><val>`           |
| Describe setting           | `s<id>?`        | `s<id>?<description>`  |
| Set setting                | `s<id><val>`    | `s<id><val>`           |

**Remark**: the settings ids (`<id>`) are single characters, and the values (`<val>`) are floating point numbers.

The driver will expose the settings as control in the INDI control panel, under the `Settings` group, and will update the settings in the firmware when the user changes them.

## Filter change status

The return of the change command `G<number>` is parsed for additional information. This information is optional, and the driver will not fail if it is not present.


| Description                | Response               |
|----------------------------|------------------------|
| Filter change success      | `G<number>:<precision>`|
| Filter change failure      | `G<number>:E<error>`   |

**Remark**: the `<precision>` is a floating point number, and the `<error>` is a string describing the error (that will be reported as is by the driver).
