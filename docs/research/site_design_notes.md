# Site design notes: three language sites, read in November 2026

Patterns only; no text, images, icons or CSS were copied. Each site was read as text and looked at in a browser at desktop and phone width.

## Microsoft Learn: C# documentation (learn.microsoft.com/dotnet/csharp)

- Landing page is a hub: a one-sentence purpose, six short entry tiles labelled by intent (get started, concept, what is new, video, training), then link lists grouped by depth (learn, fundamentals, reference, specification).
- Reading pages have three columns: a collapsible section tree on the left, the article in a measured column, and "In this article" on the right. On a phone the tree collapses behind a menu and the on-page list moves above the article, truncated with "show more".
- Breadcrumbs sit above the title; a version or product selector sits in the header; there is a search box and a theme-aware palette.
- Code blocks carry a language label and a copy button; callouts (tip, note) are boxed.
- Reference material is kept apart from learning material, and both are reachable from every page.

## perl.org

- Hero: a short claim, one number, one primary button (download and get started). Six nav labels: About, Download (with the current version shown), Learn, Docs, CPAN, Community.
- Below it, six equal cards, one per path, each with a one-line description; then a feed of recent library uploads and news.
- Very high link density and a friendly tone; the page works as a directory. Weak point: the hero spends its space on a mascot and a slogan rather than on code.
- The footer is minimal (licence and copyright).

## go.dev (chosen third site)

Chosen because it is the strongest example of "the language shows itself": one value proposition above the fold, two buttons (Get Started, Download), a runnable editor on the home page, and a short nav (Why Go, Learn, Docs, Packages, Community). It complements Learn (documentation depth) and perl.org (path cards).

- Above the fold: a headline, four short bullet claims, a primary and a secondary button, and a line saying where the packages come from.
- A runnable sample with Run, Share and a tour link sits directly under the hero.
- Use-case cards, then a strip of adopters; no news feed on the page (release notes live behind Docs).
- Footer is grouped link columns; a theme switch sits in the legal row. Phone layout keeps the hero, stacks the buttons full width and collapses the nav behind one button.

## What Objeck adopted

From Learn: the three-column reading layout with a section list and "On this page" (docs, guides, libraries); breadcrumbs on every inner page; an intent-based split (Get started, Learn, Docs, Libraries, Community); callout style for notes; the section list moving below the article and the on-page list above it on a phone; reference kept apart from tutorials; no copy button yet.

From perl.org: a short top nav that shows the current version in the page chrome (the home page's version link); path cards on one row; a latest-release strip; a calm footer. Not adopted: dense link directories on the home page.

From go.dev: one value proposition and two buttons above the fold; a runnable, real-output example next to the headline instead of an illustration; a short feature list with measured numbers only; footer in one row; buttons stack on a phone.

## Objeck specifics

- objeck.org is served by Objeck's own web server (`programs/web-site/site_server.obs`): static files, a whitelist of extensions, no dotfiles, HTML revalidated every request and everything else cacheable for a day. So the site stays static and new CSS and JS use new file names.
- No web fonts, no third-party requests on any page except the Mermaid diagrams in the architecture docs.
- Dark is the default; light follows `prefers-color-scheme: light`. Text contrast is at least 4.5:1 in both (the syntax palette is 5.1:1 to 11.6:1).
