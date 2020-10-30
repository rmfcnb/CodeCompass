import ast
from typing import Set, Any

from my_ast.class_preprocessor import PreprocessedClassCollector
from my_ast.import_preprocessor import ImportTable
from my_ast.preprocessed_function import PreprocessedFunction
from my_ast.preprocessed_variable import PreprocessedVariable


class PreprocessedFile(ast.NodeVisitor):
    def __init__(self):
        self.class_collector = PreprocessedClassCollector()
        self.preprocessed_functions: Set[PreprocessedFunction] = set()
        self.preprocessed_variables: Set[PreprocessedVariable] = set()
        self.import_table = ImportTable()
        self.depth = 0
        self.documentation: str = ""

    def visit_Module(self, node: ast.Module) -> Any:
        documentation = ast.get_docstring(node)
        if documentation is not None:
            self.documentation = documentation
        self.generic_visit(node)

    def visit_Import(self, node: ast.Import) -> Any:
        is_local = False
        if self.depth > 0:
            is_local = True
        self.import_table.append_import(node, is_local)
        self.generic_visit(node)

    def visit_ImportFrom(self, node: ast.ImportFrom) -> Any:
        is_local = False
        if self.depth > 0:
            is_local = True
        self.import_table.append_import_from(node, is_local)
        self.generic_visit(node)

    def visit_ClassDef(self, node: ast.ClassDef) -> Any:
        self.class_collector.append_class(node)
        self.depth += 1
        self.generic_visit(node)
        self.depth -= 1
        self.class_collector.class_processed()

    def visit_FunctionDef(self, node: ast.FunctionDef) -> Any:
        if self.depth == 0:
            self.preprocessed_functions.add(PreprocessedFunction(node.name))
        self.depth += 1
        self.generic_visit(node)
        self.depth -= 1

    def visit_Assign(self, node: ast.Assign) -> Any:
        if self.depth == 0:
            for target in node.targets:
                if isinstance(target, ast.Name):
                    self.preprocessed_variables.add(PreprocessedVariable(target.id))
                else:
                    print("...")
