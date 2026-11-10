#include "symbols.h"

#include <QHash>
#include <QSet>

namespace {

using A = Atom;

const QList<SymbolInfo> &table()
{
    static const QList<SymbolInfo> list = {
        // --- Greek ---------------------------------------------------------------
        {"alpha", 0x3B1, A::Ord, "greek", "alfa griega"},
        {"beta", 0x3B2, A::Ord, "greek", "beta griega"},
        {"gamma", 0x3B3, A::Ord, "greek", "gamma griega"},
        {"delta", 0x3B4, A::Ord, "greek", "delta griega"},
        {"epsilon", 0x3F5, A::Ord, "greek", "épsilon griega"},
        {"varepsilon", 0x3B5, A::Ord, "greek", "épsilon variante griega"},
        {"zeta", 0x3B6, A::Ord, "greek", "zeta dseta griega"},
        {"eta", 0x3B7, A::Ord, "greek", "eta griega"},
        {"theta", 0x3B8, A::Ord, "greek", "theta zeta griega ángulo"},
        {"vartheta", 0x3D1, A::Ord, "greek", "theta variante griega"},
        {"iota", 0x3B9, A::Ord, "greek", "iota griega"},
        {"kappa", 0x3BA, A::Ord, "greek", "kappa griega"},
        {"lambda", 0x3BB, A::Ord, "greek", "lambda griega longitud de onda"},
        {"mu", 0x3BC, A::Ord, "greek", "mu mi griega media micro"},
        {"nu", 0x3BD, A::Ord, "greek", "nu ni griega frecuencia"},
        {"xi", 0x3BE, A::Ord, "greek", "xi griega"},
        {"pi", 0x3C0, A::Ord, "greek", "pi griega"},
        {"varpi", 0x3D6, A::Ord, "greek", "pi variante griega"},
        {"rho", 0x3C1, A::Ord, "greek", "rho ro griega densidad"},
        {"varrho", 0x3F1, A::Ord, "greek", "rho variante griega"},
        {"sigma", 0x3C3, A::Ord, "greek", "sigma griega desviación"},
        {"varsigma", 0x3C2, A::Ord, "greek", "sigma final griega"},
        {"tau", 0x3C4, A::Ord, "greek", "tau griega"},
        {"upsilon", 0x3C5, A::Ord, "greek", "ípsilon griega"},
        {"phi", 0x3D5, A::Ord, "greek", "phi fi griega"},
        {"varphi", 0x3C6, A::Ord, "greek", "phi fi variante griega"},
        {"chi", 0x3C7, A::Ord, "greek", "chi ji griega"},
        {"psi", 0x3C8, A::Ord, "greek", "psi griega"},
        {"omega", 0x3C9, A::Ord, "greek", "omega griega"},
        {"Gamma", 0x393, A::Ord, "greek", "gamma mayúscula griega"},
        {"Delta", 0x394, A::Ord, "greek", "delta mayúscula griega incremento"},
        {"Theta", 0x398, A::Ord, "greek", "theta mayúscula griega"},
        {"Lambda", 0x39B, A::Ord, "greek", "lambda mayúscula griega"},
        {"Xi", 0x39E, A::Ord, "greek", "xi mayúscula griega"},
        {"Pi", 0x3A0, A::Ord, "greek", "pi mayúscula griega"},
        {"Sigma", 0x3A3, A::Ord, "greek", "sigma mayúscula griega"},
        {"Upsilon", 0x3A5, A::Ord, "greek", "ípsilon mayúscula griega"},
        {"Phi", 0x3A6, A::Ord, "greek", "phi fi mayúscula griega"},
        {"Psi", 0x3A8, A::Ord, "greek", "psi mayúscula griega"},
        {"Omega", 0x3A9, A::Ord, "greek", "omega mayúscula griega ohmio"},

        // --- Binary operators -------------------------------------------------------
        {"pm", 0xB1, A::Bin, "operators", "más menos"},
        {"mp", 0x2213, A::Bin, "operators", "menos más"},
        {"times", 0xD7, A::Bin, "operators", "por multiplicación producto aspa"},
        {"div", 0xF7, A::Bin, "operators", "entre división dividido"},
        {"cdot", 0x22C5, A::Bin, "operators", "punto producto multiplicación"},
        {"ast", 0x2217, A::Bin, "operators", "asterisco convolución"},
        {"star", 0x22C6, A::Bin, "operators", "estrella"},
        {"circ", 0x2218, A::Bin, "operators", "composición círculo grado"},
        {"bullet", 0x2219, A::Bin, "operators", "punto grueso topo"},
        {"oplus", 0x2295, A::Bin, "operators", "suma directa círculo más"},
        {"ominus", 0x2296, A::Bin, "operators", "círculo menos"},
        {"otimes", 0x2297, A::Bin, "operators", "producto tensorial círculo por"},
        {"odot", 0x2299, A::Bin, "operators", "círculo punto"},
        {"cup", 0x222A, A::Bin, "sets", "unión conjuntos"},
        {"cap", 0x2229, A::Bin, "sets", "intersección conjuntos"},
        {"setminus", 0x2216, A::Bin, "sets", "diferencia de conjuntos menos"},
        {"wedge", 0x2227, A::Bin, "sets", "y lógico conjunción cuña"},
        {"land", 0x2227, A::Bin, "", "y lógico conjunción"},
        {"vee", 0x2228, A::Bin, "sets", "o lógico disyunción"},
        {"lor", 0x2228, A::Bin, "", "o lógico disyunción"},
        {"sqcup", 0x2294, A::Bin, "operators", "unión cuadrada"},
        {"sqcap", 0x2293, A::Bin, "operators", "intersección cuadrada"},
        {"uplus", 0x228E, A::Bin, "operators", "unión disjunta más"},
        {"dagger", 0x2020, A::Bin, "operators", "daga cruz adjunto"},
        {"ddagger", 0x2021, A::Bin, "operators", "doble daga"},
        {"triangleleft", 0x25C3, A::Bin, "operators", "triángulo izquierda"},
        {"triangleright", 0x25B9, A::Bin, "operators", "triángulo derecha"},
        {"wr", 0x2240, A::Bin, "operators", "producto corona"},
        {"amalg", 0x2A3F, A::Bin, "operators", "amalgama coproducto"},

        // --- Relations --------------------------------------------------------------
        {"leq", 0x2264, A::Rel, "relations", "menor o igual"},
        {"le", 0x2264, A::Rel, "", "menor o igual"},
        {"geq", 0x2265, A::Rel, "relations", "mayor o igual"},
        {"ge", 0x2265, A::Rel, "", "mayor o igual"},
        {"neq", 0x2260, A::Rel, "relations", "distinto no igual desigual"},
        {"ne", 0x2260, A::Rel, "", "distinto no igual"},
        {"approx", 0x2248, A::Rel, "relations", "aproximadamente aproximado casi igual"},
        {"equiv", 0x2261, A::Rel, "relations", "equivalente idéntico congruente"},
        {"sim", 0x223C, A::Rel, "relations", "similar tilde distribuido"},
        {"simeq", 0x2243, A::Rel, "relations", "similar o igual"},
        {"cong", 0x2245, A::Rel, "relations", "congruente isomorfo"},
        {"propto", 0x221D, A::Rel, "relations", "proporcional"},
        {"ll", 0x226A, A::Rel, "relations", "mucho menor"},
        {"gg", 0x226B, A::Rel, "relations", "mucho mayor"},
        {"prec", 0x227A, A::Rel, "relations", "precede"},
        {"succ", 0x227B, A::Rel, "relations", "sucede"},
        {"preceq", 0x2AAF, A::Rel, "relations", "precede o igual"},
        {"succeq", 0x2AB0, A::Rel, "relations", "sucede o igual"},
        {"doteq", 0x2250, A::Rel, "relations", "igual con punto"},
        {"asymp", 0x224D, A::Rel, "relations", "asintótico"},
        {"parallel", 0x2225, A::Rel, "relations", "paralelo"},
        {"perp", 0x27C2, A::Rel, "relations", "perpendicular ortogonal"},
        {"mid", 0x2223, A::Rel, "relations", "divide tal que barra"},
        {"nmid", 0x2224, A::Rel, "relations", "no divide"},
        {"vdash", 0x22A2, A::Rel, "relations", "deduce demuestra"},
        {"dashv", 0x22A3, A::Rel, "relations", "deducción inversa"},
        {"models", 0x22A8, A::Rel, "relations", "modela satisface"},
        {"in", 0x2208, A::Rel, "sets", "pertenece elemento de"},
        {"notin", 0x2209, A::Rel, "sets", "no pertenece"},
        {"ni", 0x220B, A::Rel, "sets", "contiene como elemento"},
        {"subset", 0x2282, A::Rel, "sets", "subconjunto contenido"},
        {"supset", 0x2283, A::Rel, "sets", "superconjunto contiene"},
        {"subseteq", 0x2286, A::Rel, "sets", "subconjunto o igual contenido"},
        {"supseteq", 0x2287, A::Rel, "sets", "superconjunto o igual"},
        {"nsubseteq", 0x2288, A::Rel, "sets", "no subconjunto"},
        {"subsetneq", 0x228A, A::Rel, "sets", "subconjunto propio"},
        {"sqsubseteq", 0x2291, A::Rel, "sets", "subconjunto cuadrado"},
        {"sqsupseteq", 0x2292, A::Rel, "sets", "superconjunto cuadrado"},

        // --- Arrows -----------------------------------------------------------------
        {"to", 0x2192, A::Rel, "arrows", "flecha derecha tiende a"},
        {"rightarrow", 0x2192, A::Rel, "", "flecha derecha"},
        {"gets", 0x2190, A::Rel, "", "flecha izquierda"},
        {"leftarrow", 0x2190, A::Rel, "arrows", "flecha izquierda"},
        {"leftrightarrow", 0x2194, A::Rel, "arrows", "flecha doble sentido izquierda derecha"},
        {"Rightarrow", 0x21D2, A::Rel, "arrows", "flecha implica entonces doble derecha"},
        {"Leftarrow", 0x21D0, A::Rel, "arrows", "flecha doble izquierda implicado"},
        {"Leftrightarrow", 0x21D4, A::Rel, "arrows", "flecha si y solo si equivale doble"},
        {"implies", 0x27F9, A::Rel, "arrows", "flecha implica larga"},
        {"impliedby", 0x27F8, A::Rel, "", "flecha implicado por larga"},
        {"iff", 0x27FA, A::Rel, "arrows", "flecha si y solo si larga"},
        {"mapsto", 0x21A6, A::Rel, "arrows", "flecha aplica envía a"},
        {"longmapsto", 0x27FC, A::Rel, "arrows", "flecha aplica larga"},
        {"longrightarrow", 0x27F6, A::Rel, "arrows", "flecha larga derecha"},
        {"longleftarrow", 0x27F5, A::Rel, "arrows", "flecha larga izquierda"},
        {"longleftrightarrow", 0x27F7, A::Rel, "arrows", "flecha larga doble sentido"},
        {"uparrow", 0x2191, A::Rel, "arrows", "flecha arriba"},
        {"downarrow", 0x2193, A::Rel, "arrows", "flecha abajo"},
        {"updownarrow", 0x2195, A::Rel, "arrows", "flecha arriba abajo"},
        {"Uparrow", 0x21D1, A::Rel, "arrows", "flecha doble arriba"},
        {"Downarrow", 0x21D3, A::Rel, "arrows", "flecha doble abajo"},
        {"nearrow", 0x2197, A::Rel, "arrows", "flecha noreste diagonal"},
        {"searrow", 0x2198, A::Rel, "arrows", "flecha sureste diagonal"},
        {"swarrow", 0x2199, A::Rel, "arrows", "flecha suroeste diagonal"},
        {"nwarrow", 0x2196, A::Rel, "arrows", "flecha noroeste diagonal"},
        {"hookrightarrow", 0x21AA, A::Rel, "arrows", "flecha inclusión gancho"},
        {"hookleftarrow", 0x21A9, A::Rel, "arrows", "flecha gancho izquierda"},
        {"rightharpoonup", 0x21C0, A::Rel, "arrows", "arpón derecha"},
        {"leftharpoonup", 0x21BC, A::Rel, "arrows", "arpón izquierda"},
        {"rightleftharpoons", 0x21CC, A::Rel, "arrows", "equilibrio químico arpones"},
        {"leadsto", 0x21DD, A::Rel, "arrows", "flecha ondulada conduce"},

        // --- Big operators ---------------------------------------------------------
        {"sum", 0x2211, A::Op, "big", "sumatorio suma sumatoria", true},
        {"prod", 0x220F, A::Op, "big", "productorio producto", true},
        {"coprod", 0x2210, A::Op, "big", "coproducto", true},
        {"int", 0x222B, A::Op, "big", "integral"},
        {"iint", 0x222C, A::Op, "big", "integral doble"},
        {"iiint", 0x222D, A::Op, "big", "integral triple"},
        {"oint", 0x222E, A::Op, "big", "integral de línea cerrada contorno circulación"},
        {"bigcup", 0x22C3, A::Op, "big", "unión grande", true},
        {"bigcap", 0x22C2, A::Op, "big", "intersección grande", true},
        {"bigvee", 0x22C1, A::Op, "big", "o lógico grande disyunción", true},
        {"bigwedge", 0x22C0, A::Op, "big", "y lógico grande conjunción", true},
        {"bigoplus", 0x2A01, A::Op, "big", "suma directa grande", true},
        {"bigotimes", 0x2A02, A::Op, "big", "producto tensorial grande", true},
        {"bigodot", 0x2A00, A::Op, "big", "círculo punto grande", true},
        {"bigsqcup", 0x2A06, A::Op, "big", "unión cuadrada grande", true},
        {"biguplus", 0x2A04, A::Op, "big", "unión disjunta grande", true},

        // --- Sets and logic -----------------------------------------------------------
        {"forall", 0x2200, A::Ord, "sets", "para todo cuantificador universal"},
        {"exists", 0x2203, A::Ord, "sets", "existe cuantificador existencial"},
        {"nexists", 0x2204, A::Ord, "sets", "no existe"},
        {"emptyset", 0x2205, A::Ord, "sets", "conjunto vacío"},
        {"varnothing", 0x2205, A::Ord, "", "conjunto vacío"},
        {"neg", 0xAC, A::Ord, "sets", "negación no lógico"},
        {"lnot", 0xAC, A::Ord, "", "negación no lógico"},
        {"top", 0x22A4, A::Ord, "sets", "verdadero tautología arriba"},
        {"bot", 0x22A5, A::Ord, "sets", "falso contradicción perpendicular"},
        {"therefore", 0x2234, A::Rel, "sets", "por tanto luego"},
        {"because", 0x2235, A::Rel, "sets", "porque ya que"},

        // --- Miscellaneous ------------------------------------------------------------
        {"infty", 0x221E, A::Ord, "misc", "infinito"},
        {"partial", 0x2202, A::Ord, "misc", "derivada parcial"},
        {"nabla", 0x2207, A::Ord, "misc", "nabla gradiente divergencia"},
        {"hbar", 0x210F, A::Ord, "misc", "h barra constante de planck reducida"},
        {"ell", 0x2113, A::Ord, "misc", "ele cursiva longitud"},
        {"Re", 0x211C, A::Ord, "misc", "parte real"},
        {"Im", 0x2111, A::Ord, "misc", "parte imaginaria"},
        {"aleph", 0x2135, A::Ord, "misc", "álef cardinal"},
        {"wp", 0x2118, A::Ord, "misc", "p de weierstrass"},
        {"imath", 0x131, A::Ord, "misc", "i sin punto"},
        {"jmath", 0x237, A::Ord, "misc", "j sin punto"},
        {"angle", 0x2220, A::Ord, "misc", "ángulo"},
        {"triangle", 0x25B3, A::Ord, "misc", "triángulo"},
        {"square", 0x25A1, A::Ord, "misc", "cuadrado dalembertiano"},
        {"Box", 0x25A1, A::Ord, "", "cuadrado"},
        {"blacksquare", 0x25A0, A::Ord, "misc", "cuadrado negro fin de demostración"},
        {"diamond", 0x22C4, A::Bin, "misc", "rombo diamante"},
        {"prime", 0x2032, A::Ord, "misc", "prima derivada"},
        {"degree", 0xB0, A::Ord, "misc", "grado grados"},
        {"dots", 0x2026, A::Inner, "misc", "puntos suspensivos"},
        {"ldots", 0x2026, A::Inner, "", "puntos suspensivos bajos"},
        {"cdots", 0x22EF, A::Inner, "misc", "puntos suspensivos centrados"},
        {"vdots", 0x22EE, A::Ord, "misc", "puntos suspensivos verticales"},
        {"ddots", 0x22F1, A::Inner, "misc", "puntos suspensivos diagonales"},
        {"clubsuit", 0x2663, A::Ord, "misc", "trébol"},
        {"diamondsuit", 0x2662, A::Ord, "misc", "diamante carta"},
        {"heartsuit", 0x2661, A::Ord, "misc", "corazón"},
        {"spadesuit", 0x2660, A::Ord, "misc", "pica"},
        {"flat", 0x266D, A::Ord, "misc", "bemol"},
        {"natural", 0x266E, A::Ord, "misc", "becuadro"},
        {"sharp", 0x266F, A::Ord, "misc", "sostenido"},
        {"checkmark", 0x2713, A::Ord, "misc", "marca visto correcto"},
        {"colon", 0x3A, A::Punct, "", "dos puntos"},
        {"backslash", 0x5C, A::Ord, "", "barra invertida"},

        // --- Delimiters ----------------------------------------------------------------
        {"langle", 0x27E8, A::Open, "delimiters", "ángulo izquierdo bra producto escalar"},
        {"rangle", 0x27E9, A::Close, "delimiters", "ángulo derecho ket"},
        {"lfloor", 0x230A, A::Open, "delimiters", "suelo parte entera izquierda"},
        {"rfloor", 0x230B, A::Close, "delimiters", "suelo parte entera derecha"},
        {"lceil", 0x2308, A::Open, "delimiters", "techo izquierda"},
        {"rceil", 0x2309, A::Close, "delimiters", "techo derecha"},
        {"lbrace", 0x7B, A::Open, "delimiters", "llave izquierda"},
        {"rbrace", 0x7D, A::Close, "delimiters", "llave derecha"},
        {"lbrack", 0x5B, A::Open, "", "corchete izquierdo"},
        {"rbrack", 0x5D, A::Close, "", "corchete derecho"},
        {"vert", 0x7C, A::Ord, "delimiters", "barra vertical valor absoluto"},
        {"Vert", 0x2016, A::Ord, "delimiters", "doble barra norma"},
        {"lvert", 0x7C, A::Open, "", "barra vertical izquierda valor absoluto"},
        {"rvert", 0x7C, A::Close, "", "barra vertical derecha valor absoluto"},
        {"lVert", 0x2016, A::Open, "", "doble barra izquierda norma"},
        {"rVert", 0x2016, A::Close, "", "doble barra derecha norma"},
    };
    return list;
}

const QList<FunctionInfo> &functionTable()
{
    static const QList<FunctionInfo> list = {
        {"sin", "sin", false, "seno"},
        {"cos", "cos", false, "coseno"},
        {"tan", "tan", false, "tangente"},
        {"cot", "cot", false, "cotangente"},
        {"sec", "sec", false, "secante"},
        {"csc", "csc", false, "cosecante"},
        {"arcsin", "arcsin", false, "arcoseno"},
        {"arccos", "arccos", false, "arcocoseno"},
        {"arctan", "arctan", false, "arcotangente"},
        {"sinh", "sinh", false, "seno hiperbólico"},
        {"cosh", "cosh", false, "coseno hiperbólico"},
        {"tanh", "tanh", false, "tangente hiperbólica"},
        {"coth", "coth", false, "cotangente hiperbólica"},
        {"log", "log", false, "logaritmo"},
        {"ln", "ln", false, "logaritmo neperiano natural"},
        {"lg", "lg", false, "logaritmo decimal"},
        {"exp", "exp", false, "exponencial"},
        {"lim", "lim", true, "límite"},
        {"limsup", "lim sup", true, "límite superior"},
        {"liminf", "lim inf", true, "límite inferior"},
        {"max", "max", true, "máximo"},
        {"min", "min", true, "mínimo"},
        {"sup", "sup", true, "supremo"},
        {"inf", "inf", true, "ínfimo"},
        {"det", "det", true, "determinante"},
        {"gcd", "gcd", true, "máximo común divisor"},
        {"Pr", "Pr", true, "probabilidad"},
        {"arg", "arg", false, "argumento"},
        {"deg", "deg", false, "grado"},
        {"dim", "dim", false, "dimensión"},
        {"ker", "ker", false, "núcleo kernel"},
        {"hom", "hom", false, "homomorfismos"},
        {"bmod", "mod", false, "módulo resto"},
    };
    return list;
}

const QList<TemplateInfo> &templateTable()
{
    static const QList<TemplateInfo> list = {
        {"frac", "Fracción", "\\frac{|}{}", "structures", "fracción quebrado cociente división"},
        {"sqrt", "Raíz cuadrada", "\\sqrt{|}", "structures", "raíz cuadrada radical"},
        {"nroot", "Raíz enésima", "\\sqrt[n]{|}", "structures", "raíz enésima cúbica radical índice"},
        {"sup", "Superíndice", "^{|}", "structures", "superíndice potencia exponente elevado"},
        {"sub", "Subíndice", "_{|}", "structures", "subíndice índice"},
        {"subsup", "Sub y superíndice", "_{|}^{}", "structures", "subíndice superíndice"},
        {"binom", "Binomial", "\\binom{|}{}", "structures", "binomial combinatorio número combinaciones"},
        {"paren", "Paréntesis que crecen", "\\left( | \\right)", "structures", "paréntesis delimitador crece"},
        {"brack", "Corchetes que crecen", "\\left[ | \\right]", "structures", "corchetes delimitador crece"},
        {"brace", "Llaves que crecen", "\\left\\{ | \\right\\}", "structures", "llaves conjunto delimitador"},
        {"abs", "Valor absoluto", "\\left\\lvert | \\right\\rvert", "structures", "valor absoluto módulo barras"},
        {"norm", "Norma", "\\left\\lVert | \\right\\rVert", "structures", "norma doble barra"},
        {"sum", "Sumatorio con límites", "\\sum_{i=1}^{n} |", "calculus", "sumatorio suma límites"},
        {"prod", "Productorio con límites", "\\prod_{i=1}^{n} |", "calculus", "productorio producto límites"},
        {"int", "Integral definida", "\\int_{a}^{b} | \\, dx", "calculus", "integral definida límites"},
        {"oint", "Integral de contorno", "\\oint_{C} | \\, dl", "calculus", "integral cerrada contorno línea"},
        {"lim", "Límite", "\\lim_{x \\to \\infty} |", "calculus", "límite tiende"},
        {"deriv", "Derivada", "\\frac{d|}{dx}", "calculus", "derivada diferencial"},
        {"pderiv", "Derivada parcial", "\\frac{\\partial |}{\\partial x}", "calculus", "derivada parcial"},
        {"pmatrix", "Matriz 2×2 ( )", "\\begin{pmatrix} | & b \\\\ c & d \\end{pmatrix}", "matrices", "matriz paréntesis"},
        {"bmatrix", "Matriz 2×2 [ ]", "\\begin{bmatrix} | & b \\\\ c & d \\end{bmatrix}", "matrices", "matriz corchetes"},
        {"vmatrix", "Determinante", "\\begin{vmatrix} | & b \\\\ c & d \\end{vmatrix}", "matrices", "determinante matriz barras"},
        {"matrix3", "Matriz 3×3", "\\begin{pmatrix} | & b & c \\\\ d & e & f \\\\ g & h & i \\end{pmatrix}", "matrices", "matriz tres 3x3"},
        {"cases", "Función a trozos", "f(x) = \\begin{cases} | & \\text{si } x > 0 \\\\ 0 & \\text{si no} \\end{cases}", "matrices", "casos función a trozos llave"},
        {"aligned", "Ecuaciones alineadas", "\\begin{aligned} | &= b \\\\ &= c \\end{aligned}", "matrices", "alineadas sistema varias líneas"},
        {"vector", "Vector columna", "\\begin{pmatrix} | \\\\ y \\\\ z \\end{pmatrix}", "matrices", "vector columna"},
        {"hat", "Circunflejo", "\\hat{|}", "accents", "sombrero circunflejo gorro unitario"},
        {"bar", "Barra", "\\bar{|}", "accents", "barra media conjugado"},
        {"vec", "Vector", "\\vec{|}", "accents", "vector flecha"},
        {"dot", "Punto", "\\dot{|}", "accents", "punto derivada temporal"},
        {"ddot", "Dos puntos", "\\ddot{|}", "accents", "dos puntos segunda derivada"},
        {"tilde", "Tilde", "\\tilde{|}", "accents", "tilde virgulilla"},
        {"overline", "Línea encima", "\\overline{|}", "accents", "sobrelínea línea encima conjugado"},
        {"underline", "Subrayado", "\\underline{|}", "accents", "subrayado línea debajo"},
        {"widehat", "Circunflejo ancho", "\\widehat{|}", "accents", "sombrero ancho estimador"},
        {"overrightarrow", "Flecha encima", "\\overrightarrow{|}", "accents", "flecha encima vector"},
        {"overbrace", "Llave encima", "\\overbrace{|}^{}", "accents", "llave encima"},
        {"underbrace", "Llave debajo", "\\underbrace{|}_{}", "accents", "llave debajo"},
        {"mathbb", "Pizarra (ℝ)", "\\mathbb{|}", "fonts", "pizarra doble trazo reales naturales conjunto"},
        {"mathbf", "Negrita", "\\mathbf{|}", "fonts", "negrita vector"},
        {"mathcal", "Caligráfica", "\\mathcal{|}", "fonts", "caligráfica script"},
        {"mathrm", "Redonda", "\\mathrm{|}", "fonts", "redonda recta romana unidades"},
        {"mathfrak", "Gótica", "\\mathfrak{|}", "fonts", "gótica fraktur"},
        {"text", "Texto", "\\text{|}", "fonts", "texto palabras"},
        {"color", "Color", "\\textcolor{red}{|}", "fonts", "color rojo"},
        {"boxed", "Recuadro", "\\boxed{|}", "fonts", "recuadro caja resultado"},
    };
    return list;
}

struct Structural {
    const char *command;
    const char *insert;
    const char *title;
};

/// Commands that are neither a symbol nor a function: what the parser handles itself.
const QList<Structural> &structuralTable()
{
    static const QList<Structural> list = {
        {"frac", "\\frac{|}{}", "Fracción"},
        {"dfrac", "\\dfrac{|}{}", "Fracción grande"},
        {"tfrac", "\\tfrac{|}{}", "Fracción pequeña"},
        {"cfrac", "\\cfrac{|}{}", "Fracción continua"},
        {"binom", "\\binom{|}{}", "Binomial"},
        {"dbinom", "\\dbinom{|}{}", "Binomial grande"},
        {"tbinom", "\\tbinom{|}{}", "Binomial pequeño"},
        {"sqrt", "\\sqrt{|}", "Raíz"},
        {"left", "\\left( | \\right)", "Delimitador que crece"},
        {"right", "\\right)", "Cierra el delimitador"},
        {"middle", "\\middle\\vert ", "Delimitador intermedio"},
        {"begin", "\\begin{|}", "Entorno"},
        {"end", "\\end{|}", "Fin de entorno"},
        {"text", "\\text{|}", "Texto"},
        {"textrm", "\\textrm{|}", "Texto"},
        {"textbf", "\\textbf{|}", "Texto en negrita"},
        {"textit", "\\textit{|}", "Texto en cursiva"},
        {"mbox", "\\mbox{|}", "Texto"},
        {"mathrm", "\\mathrm{|}", "Redonda"},
        {"mathbf", "\\mathbf{|}", "Negrita"},
        {"mathit", "\\mathit{|}", "Cursiva"},
        {"mathbb", "\\mathbb{|}", "Pizarra"},
        {"mathcal", "\\mathcal{|}", "Caligráfica"},
        {"mathscr", "\\mathscr{|}", "Caligráfica"},
        {"mathfrak", "\\mathfrak{|}", "Gótica"},
        {"mathsf", "\\mathsf{|}", "Palo seco"},
        {"mathtt", "\\mathtt{|}", "Monoespaciada"},
        {"boldsymbol", "\\boldsymbol{|}", "Negrita cursiva"},
        {"bm", "\\bm{|}", "Negrita cursiva"},
        {"operatorname", "\\operatorname{|}", "Nombre de operador"},
        {"hat", "\\hat{|}", "Circunflejo"},
        {"widehat", "\\widehat{|}", "Circunflejo ancho"},
        {"bar", "\\bar{|}", "Barra"},
        {"vec", "\\vec{|}", "Vector"},
        {"dot", "\\dot{|}", "Punto"},
        {"ddot", "\\ddot{|}", "Dos puntos"},
        {"tilde", "\\tilde{|}", "Tilde"},
        {"widetilde", "\\widetilde{|}", "Tilde ancha"},
        {"check", "\\check{|}", "Anticircunflejo"},
        {"breve", "\\breve{|}", "Breve"},
        {"acute", "\\acute{|}", "Acento agudo"},
        {"grave", "\\grave{|}", "Acento grave"},
        {"overline", "\\overline{|}", "Línea encima"},
        {"underline", "\\underline{|}", "Subrayado"},
        {"overbrace", "\\overbrace{|}", "Llave encima"},
        {"underbrace", "\\underbrace{|}", "Llave debajo"},
        {"overrightarrow", "\\overrightarrow{|}", "Flecha encima"},
        {"overleftarrow", "\\overleftarrow{|}", "Flecha izquierda encima"},
        {"overset", "\\overset{|}{}", "Encima de"},
        {"underset", "\\underset{|}{}", "Debajo de"},
        {"stackrel", "\\stackrel{|}{}", "Encima de una relación"},
        {"substack", "\\substack{|}", "Varias líneas de límites"},
        {"boxed", "\\boxed{|}", "Recuadro"},
        {"color", "\\color{|}", "Color"},
        {"textcolor", "\\textcolor{|}{}", "Color de un trozo"},
        {"displaystyle", "\\displaystyle ", "Estilo de fórmula aparte"},
        {"textstyle", "\\textstyle ", "Estilo de fórmula en línea"},
        {"scriptstyle", "\\scriptstyle ", "Estilo de índice"},
        {"scriptscriptstyle", "\\scriptscriptstyle ", "Estilo de índice de índice"},
        {"limits", "\\limits", "Límites encima y debajo"},
        {"nolimits", "\\nolimits", "Límites al lado"},
        {"big", "\\big", "Delimitador grande"},
        {"Big", "\\Big", "Delimitador más grande"},
        {"bigg", "\\bigg", "Delimitador enorme"},
        {"Bigg", "\\Bigg", "Delimitador gigante"},
        {"bigl", "\\bigl", "Delimitador grande izquierdo"},
        {"bigr", "\\bigr", "Delimitador grande derecho"},
        {"Bigl", "\\Bigl", "Delimitador más grande izquierdo"},
        {"Bigr", "\\Bigr", "Delimitador más grande derecho"},
        {"biggl", "\\biggl", "Delimitador enorme izquierdo"},
        {"biggr", "\\biggr", "Delimitador enorme derecho"},
        {"Biggl", "\\Biggl", "Delimitador gigante izquierdo"},
        {"Biggr", "\\Biggr", "Delimitador gigante derecho"},
        {"bigm", "\\bigm", "Delimitador grande intermedio"},
        {"Bigm", "\\Bigm", "Delimitador más grande intermedio"},
        {"quad", "\\quad ", "Espacio de un cuadratín"},
        {"qquad", "\\qquad ", "Espacio de dos cuadratines"},
        {"enspace", "\\enspace ", "Medio cuadratín"},
        {"thinspace", "\\thinspace ", "Espacio fino"},
        {"mathrel", "\\mathrel{|}", "Como relación"},
        {"mathbin", "\\mathbin{|}", "Como operador binario"},
        {"mathop", "\\mathop{|}", "Como operador grande"},
        {"mathord", "\\mathord{|}", "Como símbolo ordinario"},
        {"not", "\\not", "Tacha el símbolo siguiente"},
        {"phantom", "\\phantom{|}", "Hueco invisible"},
        {"hline", "\\hline", "Línea horizontal"},
        {"nonumber", "\\nonumber", "Sin número"},
        {"label", "\\label{|}", "Etiqueta"},
        {"tag", "\\tag{|}", "Etiqueta"},
        {"pmod", "\\pmod{|}", "Módulo entre paréntesis"},
        {"mod", "\\mod ", "Módulo"},
    };
    return list;
}

const QHash<QString, const SymbolInfo *> &index()
{
    static const QHash<QString, const SymbolInfo *> hash = [] {
        QHash<QString, const SymbolInfo *> out;
        for (const SymbolInfo &info : table()) {
            out.insert(QString::fromLatin1(info.command), &info);
        }
        return out;
    }();
    return hash;
}

QVariantMap entry(const QString &latex, const QString &insert, const QString &title, const QString &names, const QString &glyph)
{
    return {
        {QStringLiteral("latex"), latex},
        {QStringLiteral("insert"), insert},
        {QStringLiteral("title"), title},
        {QStringLiteral("names"), names},
        {QStringLiteral("glyph"), glyph},
    };
}

QString firstName(const char *names)
{
    // «flecha derecha tiende a» is a list of words; the palette shows it whole.
    return QString::fromUtf8(names);
}

/// What the palette draws for a template: the snippet with letters where the holes are.
QString preview(const QString &latex)
{
    QString out = latex;
    out.replace(QStringLiteral("{|}{}"), QStringLiteral("{a}{b}"));
    out.replace(QStringLiteral("{|}^{}"), QStringLiteral("{a}^{b}"));
    out.replace(QStringLiteral("{|}_{}"), QStringLiteral("{a}_{b}"));
    out.replace(QStringLiteral("_{|}^{}"), QStringLiteral("x_{a}^{b}"));
    out.replace(QStringLiteral("{d|}"), QStringLiteral("{dy}"));
    out.replace(QStringLiteral("\\partial |"), QStringLiteral("\\partial f"));
    out.replace(QStringLiteral("| &"), QStringLiteral("a &"));
    out.replace(QStringLiteral("| \\\\"), QStringLiteral("x \\\\"));
    out.replace(QStringLiteral(" | "), QStringLiteral(" x "));
    out.replace(QStringLiteral("{|}"), QStringLiteral("{x}"));
    if (out.endsWith(QStringLiteral(" |"))) {
        out.chop(1);
        out += out.startsWith(QStringLiteral("\\lim")) ? QStringLiteral("f(x)") : QStringLiteral("a_i");
    }
    out.remove(QLatin1Char('|'));
    if (out.startsWith(QLatin1Char('^')) || out.startsWith(QLatin1Char('_'))) {
        out.prepend(QLatin1Char('x'));
    }
    return out;
}

}

namespace Symbols {

const SymbolInfo *find(const QString &command)
{
    return index().value(command, nullptr);
}

const SymbolInfo *byCode(char32_t code)
{
    static const QHash<char32_t, const SymbolInfo *> hash = [] {
        QHash<char32_t, const SymbolInfo *> out;
        for (const SymbolInfo &info : table()) {
            if (!out.contains(info.code)) {
                out.insert(info.code, &info);
            }
        }
        return out;
    }();
    return hash.value(code, nullptr);
}

const FunctionInfo *function(const QString &command)
{
    static const QHash<QString, const FunctionInfo *> hash = [] {
        QHash<QString, const FunctionInfo *> out;
        for (const FunctionInfo &info : functionTable()) {
            out.insert(QString::fromLatin1(info.command), &info);
        }
        return out;
    }();
    return hash.value(command, nullptr);
}

const QList<SymbolInfo> &all()
{
    return table();
}

const QList<FunctionInfo> &functions()
{
    return functionTable();
}

const QList<TemplateInfo> &templates()
{
    return templateTable();
}

QString fold(const QString &text)
{
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out += c.toLower();
        }
    }
    return out;
}

bool known(const QString &command)
{
    static const QSet<QString> structural = [] {
        QSet<QString> out;
        for (const Structural &s : structuralTable()) {
            out.insert(QString::fromLatin1(s.command));
        }
        return out;
    }();
    return find(command) || function(command) || structural.contains(command);
}

QVariantList palette(const QString &query)
{
    struct Category {
        const char *id;
        const char *title;
    };
    static const Category categories[] = {
        {"structures", "Estructuras"}, {"calculus", "Cálculo"}, {"matrices", "Matrices y sistemas"},
        {"greek", "Letras griegas"}, {"operators", "Operadores"}, {"relations", "Relaciones"},
        {"arrows", "Flechas"}, {"big", "Operadores grandes"}, {"sets", "Conjuntos y lógica"},
        {"delimiters", "Delimitadores"}, {"accents", "Acentos"}, {"fonts", "Letras y texto"},
        {"functions", "Funciones"}, {"misc", "Varios"},
    };
    const QStringList words = fold(query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto matches = [&words](const QString &haystack) {
        for (const QString &word : words) {
            if (!haystack.contains(word)) {
                return false;
            }
        }
        return true;
    };
    QVariantList out;
    for (const Category &category : categories) {
        QVariantList items;
        const QLatin1String id(category.id);
        for (const TemplateInfo &t : templateTable()) {
            if (id != QLatin1String(t.category)) {
                continue;
            }
            const QString latex = QString::fromUtf8(t.latex);
            const QString title = QString::fromUtf8(t.title);
            if (matches(fold(title + QLatin1Char(' ') + QString::fromUtf8(t.names) + QLatin1Char(' ') + latex))) {
                items << entry(preview(latex), latex, title, QString::fromUtf8(t.names), QString());
            }
        }
        for (const SymbolInfo &s : table()) {
            if (id != QLatin1String(s.category)) {
                continue;
            }
            const QString command = QLatin1Char('\\') + QString::fromLatin1(s.command);
            const QString names = firstName(s.names);
            if (matches(fold(names + QLatin1Char(' ') + command))) {
                const QString title = names.left(1).toUpper() + names.mid(1);
                items << entry(command, command + QLatin1Char(' '), title, names, QString::fromUcs4(&s.code, 1));
            }
        }
        if (id == QLatin1String("functions")) {
            for (const FunctionInfo &f : functionTable()) {
                const QString command = QLatin1Char('\\') + QString::fromLatin1(f.command);
                const QString names = QString::fromUtf8(f.names);
                if (matches(fold(names + QLatin1Char(' ') + command))) {
                    items << entry(command, command + QLatin1Char(' '), names.left(1).toUpper() + names.mid(1), names, QString::fromUtf8(f.text));
                }
            }
        }
        if (!items.isEmpty()) {
            out << QVariantMap{
                {QStringLiteral("id"), QString::fromLatin1(category.id)},
                {QStringLiteral("title"), QString::fromUtf8(category.title)},
                {QStringLiteral("items"), items},
            };
        }
    }
    return out;
}

QVariantList completions(const QString &prefix, int limit)
{
    // Exact and shorter names first: «\in» before «\int» before «\infty».
    struct Hit {
        QString command;
        QVariantMap map;
    };
    QList<Hit> hits;
    const auto take = [&](const QString &command, const QString &insert, const QString &title, const QString &glyph, const QString &latex) {
        if (!command.startsWith(prefix)) {
            return;
        }
        for (const Hit &hit : hits) {
            if (hit.command == command) {
                return;
            }
        }
        hits.append({command, {
            {QStringLiteral("command"), QString(QLatin1Char('\\') + command)},
            {QStringLiteral("insert"), insert},
            {QStringLiteral("title"), title},
            {QStringLiteral("glyph"), glyph},
            {QStringLiteral("latex"), latex},
        }});
    };
    for (const SymbolInfo &s : table()) {
        const QString command = QString::fromLatin1(s.command);
        const QString names = QString::fromUtf8(s.names);
        take(command, QLatin1Char('\\') + command + QLatin1Char(' '), names.left(1).toUpper() + names.mid(1), QString::fromUcs4(&s.code, 1), QLatin1Char('\\') + command);
    }
    for (const FunctionInfo &f : functionTable()) {
        const QString command = QString::fromLatin1(f.command);
        const QString names = QString::fromUtf8(f.names);
        take(command, QLatin1Char('\\') + command + QLatin1Char(' '), names.left(1).toUpper() + names.mid(1), QString(), QLatin1Char('\\') + command);
    }
    for (const Structural &s : structuralTable()) {
        const QString insert = QString::fromUtf8(s.insert);
        const QString shown = insert.contains(QLatin1Char('|')) && !insert.contains(QStringLiteral("begin")) && !insert.contains(QStringLiteral("end{"))
            && !insert.contains(QStringLiteral("color")) && !insert.contains(QStringLiteral("label")) && !insert.contains(QStringLiteral("left"))
            ? preview(insert) : QString();
        take(QString::fromLatin1(s.command), insert, QString::fromUtf8(s.title), QString(), shown);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        // The table's order within a length: the common ones come first there.
        return a.command.size() < b.command.size();
    });
    QVariantList out;
    for (const Hit &hit : hits) {
        if (out.size() >= limit) {
            break;
        }
        out << hit.map;
    }
    return out;
}

}
