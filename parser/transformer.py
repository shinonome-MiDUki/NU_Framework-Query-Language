from lark import Lark, Transformer

with open(r"/Users/shiinaayame/Documents/NU_Framework_Query_Language_Proj/NU_Framework-Query-Language/syntax/nuql_syntax.lark", "r", encoding="utf-8") as syn:
    nuql_grammar = syn.read()


class NUQLTransformer(Transformer):
    def inclusion(self, children):
        return str(children[0])
    
    def mode_spec(self, children):
        version = str(children[2])
        mode = str(children[3])
        return version, mode

    def cluster(self, children):
        modifier = children[0] if isinstance(children[0], list) else []
        clu_name = str(children[2])
        inheritance = children[3]
        data = children[-2] 
        return {
            "modifier": modifier,
            "clu_name": clu_name,
            "inheritance": inheritance,
            "data": data
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
    
    def RESERVE(self, token):
        return str(token)
    
    def IS_REJECTED(self, token):
        return str(token)
    
    def inheritance(self, children):
        if len(children) > 1:
            modifiers = [str(token) for token in children[:-1] if isinstance(token, str)]
            inheritance_name = str(children[-1])
            return modifiers, inheritance_name
        else:
            return [], str(children[0])
    
    def inheritance_list(self, children):
        if len(children) > 3:
            return children[1:-1]  
        else:
            return []
        
    def diff(self, children):
        diff_name = str(children[2])
        diff_content = children[-2]
        return {
            "diff_name": diff_name,
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

    def COMMA(self, token):
        return str(token)
    
    def COLON(self, token):
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

    def oneway_pair(self, children):
        lhs = str(children[0])
        rhs = str(children[2])
        return {
            "lhs": lhs,
            "rhs": rhs
        }
    
    def twoway_pair(self, children):
        lhs = str(children[0])
        rhs = [str(token) for token in children[2:]]
        return {
            "lhs": lhs,
            "rhs": rhs
        }
    
    def identifiable_value(self, children):
        return {
            "type": "id",
            "value": str(children[-1])
        }

    def logical_value(self, children):
        if children[0] == "%":
            data_type = "bool"
            data_value = children[-1] == "true"
        elif children[0] == "%":
            data_type = "num"
            data_value = str(children[-1])
            data_value = float(data_value) if "." in data_value else int(data_value)
        else:
            return None
        return {
            "type" : data_type,
            "value": data_value
        }
    
    def flexible_value(self, children):
        if children[0] == "r" and children[1] == "{" and children[-1] == "}":
            data_type = "regex"
            data_value = str(children[-2])
        elif children[0] == "%" and children[-1] == "%":
            data_type = "fstring"
            data_value = str(children[-2])
        else:
            return None
        return {
            "type" : data_type,
            "value": data_value
        }
    
    def collection_value(self, children):
        if children[1] != "{" or children[-1] != "}":
            return None
        if children[0] == "s":
            data_type = "set"
            data_value = set([token["value"] for token in children[2:-1]])
        elif children[0] == "j":
            data_type = "json"
            data_value = str(children[-2])
        else:
            return None
        return {
            "type" : data_type,
            "value" : data_value
        }
    
    def injected_type(self, children):
        if children[0] == "inj" and children[1] == ".":
            extension = str(children[2])
            type_name = str(children[3])
            value = str(children[4])
            return {
                "type": "injected",
                "extension": extension,
                "type_name": type_name,
                "value": value
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
        return {
            "type": "id",
            "value": str(token)
        }
    
    def NAME(self, token):
        return str(token)
    
    def PATH(self, token):
        return str(token)
    
    def EXTENSION(self, token):
        return str(token)
    
    def TYPE_NAME(self, token):
        return str(token)
    
    def STRING(self, token):
        return {
            "type": "str",
            "value": str(token)[1:-1]
        }
    
    def STRCIT_STRING(self, token):
        return {
            "type": "sstr",
            "value": str(token)[1:-1]
        }
    
    def JSON_CONTENT(self, token):
        return str(token)
    
    def COMMENT(self, token):
        return None

def parse_nuql(text):
    parser = Lark(nuql_grammar, start='start', parser='lalr')
    tree = parser.parse(text)
    result = NUQLTransformer().transform(tree)
    return result

# テスト実行
with open(r"/Users/shiinaayame/Documents/NU_Framework_Query_Language_Proj/NU_Framework-Query-Language/official_sample/sample.nuql", "r", encoding="utf-8") as f:
    sample_nuql = f.read()
print(parse_nuql(sample_nuql))