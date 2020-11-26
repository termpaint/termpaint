<h1 align="center">
    Termpaint
</h1>


<h3 align="center">
  <a href="https://termpaint.namepad.de/latest/">Documentation</a>
  <span> · </span>
  <a href="https://termpaint.namepad.de/latest/getting-started.html">Getting Started</a>
  <span> · </span>
</h3>

Low level terminal interface library for modern terminals.


## Documentation

The full documentation for Termpaint can be found [here](https://termpaint.namepad.de/latest/).

## Building / Installing

    $ meson setup -Dprefix=$HOME/opt/termpaint/ _build
    $ ninja -C _build
    $ ninja -C _build install

## Example

See [Getting started](https://termpaint.namepad.de/latest/getting-started.html) or [full source](doc/getting-started.c).

    integration = termpaintx_full_integration_setup_terminal_fullscreen(
                "+kbdsig +kbdsigint",
                event_callback, &quit,
                &terminal);
    surface = termpaint_terminal_get_surface(terminal);
    termpaint_surface_clear(surface,
                TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    termpaint_surface_write_with_colors(surface,
                0, 0,
                "Hello World",
                TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);

    termpaint_terminal_flush(terminal, false);

    while (!quit) {
        if (!termpaintx_full_integration_do_iteration(integration)) {
            // some kind of error
            break;
        }
    }

    termpaint_terminal_free_with_restore(terminal);

## Included examples

* attrdemo [demo/attrs.c](demo/attrs.c)  
  Show attributes and colors.
* playground [playground2.cpp](playground2.cpp)  
  Show keyboard events.
* life [demo/life.c](demo/life.c)  
  A simple "Conway's Game of Life" demo.
* shuffle [demo/shuffle.c](demo/shuffle.c)  
  A simple shuffle numbers demo.

## Why?

See this [blog post](https://tty.uchuujin.de/2020/11/journey-of-termpaint/).

## License

Termpaint is licensed under the [Boost Software License 1.0](COPYING)