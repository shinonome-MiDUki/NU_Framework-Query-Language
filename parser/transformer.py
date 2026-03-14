from lark import Lark, Transformer

with open(r"/Users/shiinaayame/Documents/NU_Framework_Query_Language_Proj/NU_Framework-Query-Language/syntax/nuql_syntax.lark", "r", encoding="utf-8") as syn:
    nuql_grammar = syn.read()


class NUQLTransformer(Transformer):
    # 各ルール名と同じ名前のメソッドを作ると、解析時に自動で呼ばれます
    
    def identifier(self, tokens):
        return str(tokens[0])

    def STRING(self, token):
        return str(token)

    def oneway_pair(self, items):
        # items = [key, value]
        return {"type": "one_way", "key": items[0], "value": items[1]}

    def cluster(self, items):
        # itemsには [modifier, name, inheritance, content] などが入る
        # 構造に合わせて辞書を組み立てる
        return {
            "name": items[1],
            "body": items[-1]
        }

# 3. 実行処理
def parse_nuql(text):
    # パーサーの初期化（lalrは高速ですが、文法に厳格です。柔軟性が欲しい場合はearleyを選びます）
    parser = Lark(nuql_grammar, start='start', parser='lalr')
    
    # テスト解析
    tree = parser.parse(text)
    
    # データの変換
    result = NUQLTransformer().transform(tree)
    return result

# テスト実行
test_data = "clu User << name = Tanaka ; age = @25 >>"
print(parse_nuql(test_data))