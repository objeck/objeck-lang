# Objeck Website

Source files for [objeck.org](https://www.objeck.org) — the static landing site for the Objeck programming language.

## Structure

```
web/
├── index.html             # Landing page
├── getting_started.html   # Quick start guide
├── readme.html            # Changelog
└── style/
    ├── styles.css         # Shared stylesheet
    ├── objeck-logo.png    # Logo
    └── favicon.ico        # Favicon
```

## Deployment

The site is static HTML/CSS hosted on GoDaddy. Upload the files via the GoDaddy file manager or FTP.

API documentation (`doc/api/`) is generated separately and not tracked in this directory.

## Related

- **Web Playground**: [`programs/web-playground/`](../../programs/web-playground/) — the browser-based code editor and sandbox at [playground.objeck.org](https://playground.objeck.org)
