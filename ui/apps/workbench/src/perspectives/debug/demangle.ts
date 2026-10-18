// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A demangler for the Itanium C++ ABI names firmware carries (Arduino's
// classes, a sketch's static functions): nested and unscoped names, internal
// linkage (L), constructors and destructors, const member functions, the
// builtin types, pointers, references, const, the standard abbreviations,
// substitutions (S_, S0_, ...) and simple template arguments. A name outside
// that subset (operators, function types, templates of templates) comes back
// as it was: the debugger never shows a wrong name.

const BUILTIN: Record<string, string> = {
  v: 'void', w: 'wchar_t', b: 'bool', c: 'char', a: 'signed char', h: 'unsigned char', s: 'short',
  t: 'unsigned short', i: 'int', j: 'unsigned int', l: 'long', m: 'unsigned long', x: 'long long',
  y: 'unsigned long long', n: '__int128', o: 'unsigned __int128', f: 'float', d: 'double',
  e: 'long double', g: '__float128', z: '...', Dn: 'decltype(nullptr)', Ds: 'char16_t', Di: 'char32_t',
};

const ABBREV: Record<string, string> = {
  St: 'std', Sa: 'std::allocator', Sb: 'std::basic_string', Ss: 'std::string', Si: 'std::istream',
  So: 'std::ostream', Sd: 'std::iostream',
};

class Fail extends Error {}

class Parser {
  i = 0;
  subs: string[] = [];
  readonly s: string;
  constructor(s: string) {
    this.s = s;
  }

  peek(n = 1): string {
    return this.s.slice(this.i, this.i + n);
  }

  eat(t: string): boolean {
    if (this.s.startsWith(t, this.i)) {
      this.i += t.length;
      return true;
    }
    return false;
  }

  number(): number {
    const m = /^\d+/.exec(this.s.slice(this.i));
    if (!m) throw new Fail();
    this.i += m[0].length;
    return Number(m[0]);
  }

  sourceName(): string {
    const n = this.number();
    const id = this.s.slice(this.i, this.i + n);
    if (id.length !== n) throw new Fail();
    this.i += n;
    return id.startsWith('_GLOBAL__N') ? '(anonymous namespace)' : id;
  }

  substitution(): string {
    if (!this.eat('S')) throw new Fail();
    if (this.eat('_')) return this.sub(0);
    const m = /^[0-9A-Z]+(?=_)/.exec(this.s.slice(this.i));
    if (!m) throw new Fail();
    this.i += m[0].length + 1;
    return this.sub(parseInt(m[0], 36) + 1);
  }

  sub(k: number): string {
    const v = this.subs[k];
    if (v === undefined) throw new Fail();
    return v;
  }

  templateArgs(): string {
    if (!this.eat('I')) throw new Fail();
    const args: string[] = [];
    while (!this.eat('E')) {
      if (this.peek() === 'L') {
        // a literal: L <type> <value> E
        this.i++;
        const ty = this.type();
        const m = /^n?\d+/.exec(this.s.slice(this.i));
        if (!m) throw new Fail();
        this.i += m[0].length;
        if (!this.eat('E')) throw new Fail();
        const v = m[0].replace(/^n/, '-');
        args.push(ty === 'bool' ? (v === '0' ? 'false' : 'true') : ty === 'int' ? v : `(${ty})${v}`);
      } else {
        args.push(this.type());
      }
    }
    const list = args.join(', ');
    return `<${list}${list.endsWith('>') ? ' ' : ''}>`;
  }

  /** a nested name: N [K] prefix... E. Each prefix short of the whole name
   * is a substitution candidate (the whole is one too when it is a type,
   * which type() adds); returns [qualified name, const?] */
  nested(): [string, boolean] {
    if (!this.eat('N')) throw new Fail();
    let isConst = false;
    while (this.peek() === 'r' || this.peek() === 'V' || this.peek() === 'K') {
      if (this.s[this.i] === 'K') isConst = true;
      this.i++;
    }
    const prefixes: string[] = [];
    let name = '';
    let last = '';
    let fresh = false; // `name` is a new candidate (not a substitution as it stands)
    while (!this.eat('E')) {
      if (fresh) prefixes.push(name);
      fresh = true;
      if (this.peek(2) in ABBREV && this.peek(2) !== 'St') {
        name = ABBREV[this.peek(2)]!;
        this.i += 2;
        last = name.slice(name.lastIndexOf(':') + 1);
        fresh = false;
      } else if (this.eat('St')) {
        name = 'std';
        fresh = false;
      } else if (this.peek() === 'S') {
        name = this.substitution();
        last = name.slice(name.lastIndexOf(':') + 1).replace(/<.*$/, '');
        fresh = false;
      } else if (this.peek() === 'I') {
        name += this.templateArgs();
      } else if (/^C[123]/.test(this.peek(2))) {
        this.i += 2;
        name = `${name}::${last}`;
      } else if (/^D[012]/.test(this.peek(2))) {
        this.i += 2;
        name = `${name}::~${last}`;
      } else if (/^L?\d/.test(this.peek(2))) {
        this.eat('L');
        last = this.sourceName();
        name = name ? `${name}::${last}` : last;
      } else {
        throw new Fail();
      }
    }
    this.subs.push(...prefixes);
    return [name, isConst];
  }

  type(): string {
    const c = this.peek();
    const two = this.peek(2);
    if (two.length === 2 && two in BUILTIN) {
      this.i += 2;
      return BUILTIN[this.s.slice(this.i - 2, this.i)]!;
    }
    if (c in BUILTIN) {
      this.i++;
      return BUILTIN[c]!;
    }
    let t: string;
    if (c === 'P' || c === 'R' || c === 'O' || c === 'K') {
      this.i++;
      const inner = this.type();
      t = c === 'K' ? `${inner} const` : `${inner}${c === 'P' ? '*' : c === 'R' ? '&' : '&&'}`;
    } else if (c === 'N') {
      t = this.nested()[0];
    } else if (c === 'S' && this.peek(2) in ABBREV && this.peek(2) !== 'St') {
      t = ABBREV[this.peek(2)]!;
      this.i += 2;
      if (this.peek() === 'I') t += this.templateArgs();
      else return t;
    } else if (c === 'S' && this.peek(2) === 'St') {
      this.i += 2;
      t = `std::${this.sourceName()}`;
      if (this.peek() === 'I') {
        this.subs.push(t);
        t += this.templateArgs();
      }
    } else if (c === 'S') {
      t = this.substitution();
      if (this.peek() === 'I') t += this.templateArgs();
      else return t; // a substitution is not a new candidate
    } else if (/\d/.test(c)) {
      t = this.sourceName();
      if (this.peek() === 'I') {
        this.subs.push(t);
        t += this.templateArgs();
      }
    } else if (c === 'A') {
      this.i++;
      const n = this.number();
      if (!this.eat('_')) throw new Fail();
      t = `${this.type()} [${n}]`;
    } else {
      throw new Fail();
    }
    this.subs.push(t);
    return t;
  }
}

/** `name` demangled, or `name` when it is not a C++ name or outside the subset */
export function demangle(name: string): string {
  if (!name.startsWith('_Z')) return name;
  try {
    const p = new Parser(name);
    p.i = 2;
    let fn: string;
    let isConst = false;
    if (p.peek() === 'N') {
      [fn, isConst] = p.nested();
    } else {
      p.eat('L');
      if (p.eat('St')) fn = `std::${p.sourceName()}`;
      else fn = p.sourceName();
      if (p.peek() === 'I') {
        p.subs.push(fn);
        fn += p.templateArgs();
        // a template function's encoding starts with its return type
        p.type();
      }
    }
    // a clone suffix (.constprop.0, .isra.0, .part.0) stays visible
    const dot = name.indexOf('.', p.i);
    const end = dot < 0 ? name.length : dot;
    const params: string[] = [];
    while (p.i < end) params.push(p.type());
    if (p.i !== end) return name;
    const list = params.length === 1 && params[0] === 'void' ? '' : params.join(', ');
    return `${fn}(${list})${isConst ? ' const' : ''}${dot < 0 ? '' : ` [clone ${name.slice(dot)}]`}`;
  } catch (e) {
    if (e instanceof Fail) return name;
    throw e;
  }
}
