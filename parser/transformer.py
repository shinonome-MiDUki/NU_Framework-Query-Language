from lark import Lark, Transformer, Token, Tree

with open(r"/Users/shiinaayame/Documents/NU_Framework_Query_Language_Proj/NU_Framework-Query-Language/syntax/nuql_syntax.lark", "r", encoding="utf-8") as syn:
    nuql_grammar = syn.read()


class NUQLTransformer(Transformer):
    def inclusion(self, children):
        return str(children[0])
    
    def mode_spec(self, children):
        version = str(children[2]) if len(children) > 2 else None
        mode    = str(children[3]) if len(children) > 3 else None
        return version, mode

    def cluster(self, children):
        modifier     = next((c for c in children if isinstance(c, list) and
                             all(isinstance(x, str) for x in c)), [])
        clu_name     = next((c for c in children if isinstance(c, str) and c not in
                             ("clu", "<<", ">>")), None)
        inheritance  = next((c for c in children if isinstance(c, list) and
                     any(hasattr(x, 'data') for x in c)), None)
        data         = next((c for c in children if isinstance(c, list) and
                            c is not modifier and c is not inheritance), None)
        return {
            "modifier":    modifier,
            "clu_name":    clu_name,
            "inheritance": inheritance,
            "data":        data
        }

    def meta_cluster(self, children):
        meta_type = str(children[0])
        meta_data = children[-2]
        return {
            "meta_type": meta_type,
            "meta_data": meta_data
        }
    
    def header_cluster(self, children):
        header_name = str(children[2])
        header_data = children[-2]
        return {
            "header_name": header_name,
            "header_data": header_data
        }
    
    def cluster_modifier(self, children):
        return [str(token) for token in children]
    
    def IS_PRIME(self, token):
        return str(token)
    
    def IS_REJECTED(self, token):
        return str(token)
    
    def inheritance(self, children):
        return [str(c) for c in children]
    
    def inheritance_list(self, children):
        inner = children[1:-1]
        return [c for c in inner if str(c) != ","]
        
    def diff(self, children):
        diff_name    = str(children[2])
        diff_content = children[-2]
        return {
            "diff_name":    diff_name,
            "diff_content": diff_content
        }
    
    def diff_cluster(self, children):
        return [child for child in children if isinstance(child, dict) and "diff_name" in child]
    
    def PROTECTED(self, token):
        return str(token)
    
    def SIDEWAY(self, token):
        return str(token)
    
    def ONE_WAY(self, token):
        return str(token)
    
    def TWO_WAY(self, token):
        return str(token)

    def content(self, children):
        return [child for child in children if isinstance(child, dict)]
    
    def header_content(self, children):
        return [child for child in children if isinstance(child, dict)]   
    
    def datum_modifiers(self, children):
        return [str(token) for token in children] if children else []
    
    def datum(self, children):
        modifiers = [str(token) for token in children[:-1] if isinstance(token, str)]
        pair = children[-1]
        return {
            "modifiers": modifiers,
            "pair": pair
        }

    @staticmethod
    def _resolve_value(children, start_index=0):
        """
        CHANGE 3: `?` 付きルール（identifiable_value / logical_value /
        flexible_value / collection_value）はLarkによりインライン展開されるため、
        Transformerメソッドとして捕捉できない。
        このヘルパーで各 one-way / two-way ペアのオペランドを直接解釈する。
        children はルール全体の子リスト、start_index は解釈開始位置。
        """
        c = children[start_index]
        s = str(c)

        if isinstance(c, Token) and c.type == "STRING":
            return {"type": "str", "value": s[1:-1]}

        if isinstance(c, Token) and c.type == "STRICT_STRING":
            return {"type": "sstr", "value": s[1:-1]}
        
        if isinstance(c, Tree):
            rule = c.data if isinstance(c.data, str) else str(c.data)
            if rule == "identifiable_value":
                return {"type": "id", "value": str(c.children[1])}
            if rule == "logical_value":
                raw = str(c.children[1])
                return {"type": "num", "value": float(raw) if "." in raw else int(raw)}
            if rule == "collection_value":
                items = [NUQLTransformer._resolve_value([ch]) for ch in c.children
                        if isinstance(ch, Token) and ch.type == "STRING"]
                return {"type": "set", "value": set(it["value"] for it in items if it)}

        if isinstance(c, dict):
            return c

        if s == "#" and start_index + 1 < len(children):
            return {"type": "id", "value": str(children[start_index + 1])}

        if s == "@" and start_index + 1 < len(children):
            raw = str(children[start_index + 1])
            return {"type": "num", "value": float(raw) if "." in raw else int(raw)}

        if s == "/" and start_index + 1 < len(children):
            nxt = str(children[start_index + 1])
            if nxt in ("true", "false"):
                return {"type": "bool", "value": nxt == "true"}

        if s == "r{" and start_index + 1 < len(children):
            return {"type": "regex", "value": str(children[start_index + 1])}

        if s == "%" and start_index + 1 < len(children):
            return {"type": "fstring", "value": str(children[start_index + 1])}

        if s == "s{":
            items = []
            i = start_index + 1
            while i < len(children) and str(children[i]) != "}":
                item = NUQLTransformer._resolve_value(children, i)
                if item:
                    items.append(item)
                i += 1
            return {"type": "set", "value": set(it["value"] for it in items if isinstance(it, dict))}

        if s == "j{":
            nxt = children[start_index + 1]
            if str(nxt) == "^":
                return {"type": "json", "value": str(children[start_index + 2])}
            return {"type": "json", "value": str(nxt)}

        if isinstance(c, Token) and c.type in ("IDENTIFIER", "NAME"):
            return {"type": "id", "value": s}

        return None

    def oneway_pair(self, children):
        print("oneway children:", children)
        sep = next(i for i, c in enumerate(children) if str(c) == "=" and str(c) != "==")
        lhs = self._resolve_value(children, 0)
        rhs = self._resolve_value(children, sep + 1)
        return {"lhs": lhs, "rhs": rhs}

    def twoway_pair(self, children):
        print("twoway children:", children)
        sep = next(i for i, c in enumerate(children) if str(c) == "==" and str(c) != "=")
        lhs = self._resolve_value(children, 0)
        rhs_tokens = children[sep + 1:]
        rhs = []
        i = 0
        while i < len(rhs_tokens):
            if str(rhs_tokens[i]) == "|":
                i += 1
                continue
            val = self._resolve_value(rhs_tokens, i)
            if val:
                rhs.append(val)
            i += 1
        return {"lhs": lhs, "rhs": rhs}
    
    def identifiable_value(self, children):
        if str(children[0]) == "#":
            return {"type": "id", "value": str(children[1])}
        return children[0] 

    def logical_value(self, children):
        raw = str(children[1])
        return {"type": "num", "value": float(raw) if "." in raw else int(raw)}
    
    def flexible_value(self, children):
        if str(children[0]) == "r{":
            return {"type": "regex", "value": str(children[1])}
        return {"type": "fstring", "value": str(children[1])}

    def collection_value(self, children):
        items = [ch for ch in children if isinstance(ch, Token) and ch.type == "STRING"]
        return {"type": "set", "value": set(str(it)[1:-1] for it in items)}

    def injected_type(self, children):
        if str(children[0]) == "inj" and str(children[1]) == ".":
            extension = str(children[2])
            type_name = str(children[3])
            value     = str(children[4])
            return {
                "type":      "injected",
                "extension": extension,
                "type_name": type_name,
                "value":     value
            }
        else:
            return None

    def group(self, children):
        group_name = str(children[1])
        group_data = children[3:-1]
        return {
            "group_name": group_name,
            "group_data": group_data
        }
    
    def IDENTIFIER(self, token):
        return str(token)
    
    def NAME(self, token):
        return str(token)
    
    def EXTENSION(self, token):
        return str(token)
    
    def TYPE_NAME(self, token):
        return str(token)
    
    def STRING(self, token):
        return token  
    
    def STRICT_STRING(self, token):
        return token 
    
    def JSON_CONTENT(self, token):
        return str(token)
    
    def COMMENT(self, token):
        return None

def parse_nuql(text):
    parser = Lark(nuql_grammar, start='start', parser='lalr', keep_all_tokens=True)
    tree = parser.parse(text)
    result = NUQLTransformer().transform(tree)
    return result

with open(r"/Users/shiinaayame/Documents/NU_Framework_Query_Language_Proj/NU_Framework-Query-Language/official_sample/sample.nuql", "r", encoding="utf-8") as f:
    sample_nuql = f.read()
from pprint import pprint
pprint("=== Parsed NUQL ===")
pprint(parse_nuql(sample_nuql), width=120)