// Pictures for the SVG test, kept out of the file moc reads.
#pragma once

namespace Samples
{
inline const char Basic[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="400" height="300">
            <rect x="10" y="20" width="100" height="50" rx="8" fill="#ff0000" stroke="#0000ff" stroke-width="3"/>
            <circle cx="200" cy="100" r="40" fill="none" stroke="black"/>
            <ellipse cx="300" cy="200" rx="60" ry="20" style="fill:rgb(0,128,0);opacity:0.5"/>
            <line x1="0" y1="0" x2="50" y2="80" stroke="#333" stroke-dasharray="6 4"/>
            <polyline points="0,0 10,10 20,0" fill="none" stroke="red"/>
            <polygon points="100,100 150,100 125,150" fill="yellow"/>
            <path d="M10 250 C 40 200 80 300 110 250" fill="none" stroke="purple" stroke-width="4"/>
            <text x="20" y="290" font-size="18" font-weight="bold" fill="#112233">Hola SVG</text>
        </svg>)svg";

inline const char Grouped[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 100 100" width="200" height="200">
            <defs><rect id="tile" width="10" height="10"/></defs>
            <g transform="translate(10 20)" fill="#00ff00" stroke="#000000">
                <rect width="10" height="10"/>
                <g transform="scale(2)"><rect x="5" y="5" width="10" height="10" fill="blue"/></g>
            </g>
            <rect x="40" y="40" width="20" height="10" transform="rotate(90 50 45)"/>
            <use xlink:href="#tile" x="70" y="70"/>
        </svg>)svg";

inline const char Styled[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
            <style>.warm { fill: #ff8800; stroke: none } #cold { fill: #0088ff }</style>
            <defs><linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
                <stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#0000ff"/></linearGradient></defs>
            <rect class="warm" width="10" height="10"/>
            <rect id="cold" width="10" height="10"/>
            <rect width="10" height="10" fill="url(#sky)"/>
            <rect width="10" height="10" display="none"/>
            <rect width="10" height="10" fill="currentColor" color="#abcdef" fill-opacity="0.5"/>
        </svg>)svg";


/// The way LibreOffice writes a text: spans inside spans.
inline const char NestedSpans[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 200">
<text class="SVGTextShape"><tspan class="TextParagraph"><tspan class="TextPosition" x="40" y="80"><tspan font-size="24px" font-weight="700" fill="rgb(200,0,0)" stroke="none">Proceso</tspan></tspan></tspan></text>
</svg>)svg";
}
