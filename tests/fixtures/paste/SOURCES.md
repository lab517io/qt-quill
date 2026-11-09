# Clipboard HTML fixtures: provenance

Hand-built from the clipboard formats each source is documented and known to
produce (structure, attributes and markers as those applications write
them), not captured on the platforms themselves: no Windows or macOS machine
was available (milestone M9a). Replace with real captures in M9 and keep the
expected Deltas in sync (`QUILLQT_WRITE_FIXTURES=1 tst_paste`, then review).

| File | Imitates |
|---|---|
| `windows_cf_html.html` | Windows CF_HTML as Qt delivers it (header stripped, `<!--StartFragment-->` markers kept) |
| `windows_cf_html_raw_header.html` | CF_HTML with its `Version:0.9 StartHTML:...` header still in the text/html data |
| `macos_safari.html` | Safari / WebKit: `<meta charset>`, computed inline styles, `Apple-converted-space` |
| `word.html` | Microsoft Word: `MsoNormal`, `mso-list` paragraphs with `<![if !supportLists]>` markers, `<o:p>` |
| `libreoffice.html` | LibreOffice Writer HTML export |
| `google_docs.html` | Google Docs: `<b style="font-weight:normal" id="docs-internal-guid-...">` wrapper, styled spans |
| `chrome.html` | Chrome copying from a web page: computed styles, link, inline code, list, quote, pre |

`*.expected.json` is the Delta `QuillEditor` pastes into an empty document.
