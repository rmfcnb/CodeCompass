#include <iostream>
#include <Python.h>

#include <boost/filesystem.hpp>
#include <boost/python.hpp>
#include <boost/optional.hpp>
#include <boost/none.hpp>

#include <odb/database.hxx>

#include <pythonparser/pythonparser.h>

#include <util/hash.h>
#include <util/odbtransaction.h>

#include <parser/sourcemanager.h>

#include <model/file.h>
#include <model/fileloc.h>
#include <model/buildsourcetarget.h>
#include <model/buildsourcetarget-odb.hxx>

#include <model/pythonastnode.h>
#include <model/pythonastnode-odb.hxx>
#include <model/pythonclass.h>
#include <model/pythonclass-odb.hxx>
#include <model/pythondocumentation.h>
#include <model/pythondocumentation-odb.hxx>
#include <model/pythonfunction.h>
#include <model/pythonfunction-odb.hxx>
#include <model/pythoninheritance.h>
#include <model/pythoninheritance-odb.hxx>
#include <model/pythonimport.h>
#include <model/pythonimport-odb.hxx>
#include <model/pythontype.h>
#include <model/pythontype-odb.hxx>
#include <model/pythonvariable.h>
#include <model/pythonvariable-odb.hxx>
#include <model/pythonentity-odb.hxx>

namespace cc {
namespace parser{

void print(const std::string& s) {
    std::cout << s << std::endl;
}

class Persistence
{
private:
    ParserContext& ctx;

public:
    Persistence(ParserContext& ctx_) : ctx(ctx_) {}
    ~Persistence();

    void cppprint(boost::python::object o);
    void persistFile(boost::python::object pyFile);
    void persistVariable(boost::python::object pyVariable);
    void persistFunction(boost::python::object pyFunction);
    void persistPreprocessedClass(boost::python::object pyClass);
    void persistClass(boost::python::object pyClass);
    void persistImport(boost::python::object pyImport);

private:
    boost::optional<model::FileLoc> createFileLocFromPythonFilePosition(boost::python::object filePosition);
    model::PythonEntityPtr getPythonEntity(const std::string& qualifiedName);
    model::PythonClassPtr getPythonClass(const std::string& qualifiedName);
    model::PythonVariablePtr getPythonVariable(const std::string& qualifiedName);

    std::vector<model::PythonAstNodePtr> _astNodes;
    std::vector<model::PythonVariablePtr> _variables;
    std::map<model::PythonEntityId, std::vector<model::PythonAstNodePtr>> _variableUsages;
    std::vector<model::PythonFunctionPtr> _functions;
    std::map<model::PythonEntityId, std::vector<model::PythonAstNodePtr>> _functionUsages;
    std::vector<model::PythonClassPtr> _classes;
    std::map<model::PythonEntityId, std::vector<model::PythonAstNodePtr>> _classUsages;
    std::vector<model::PythonClassMemberPtr> _members;
    std::vector<model::PythonInheritancePtr> _inheritance;
    std::vector<model::PythonImportPtr> _imports;
    std::vector<model::PythonDocumentationPtr> _documentations;
    std::vector<model::PythonTypePtr> _types;
};

template <typename T, typename U>
static std::vector<T> getKeys(const std::map<T, U>& map) {
    std::vector<T> ret;
    std::for_each(map.begin(), map.end(), [&ret](const auto& p){ ret.push_back(p.first); });
    return ret;
}

Persistence::~Persistence()
{
    std::map<int, bool> m;
    std::vector<int> v = getKeys(m);
    ctx.srcMgr.persistFiles();

    (util::OdbTransaction(ctx.db))([this]{
      util::persistAll(_astNodes, ctx.db);
      for(auto& ast : _variableUsages){
          util::persistAll(ast.second, ctx.db);
      }
      for(auto& ast : _functionUsages){
          util::persistAll(ast.second, ctx.db);
      }
      for(auto& ast : _classUsages){
          util::persistAll(ast.second, ctx.db);
      }
      util::persistAll(_variables, ctx.db);
      util::persistAll(_functions, ctx.db);
      util::persistAll(_classes, ctx.db);
      util::persistAll(_members, ctx.db);
      util::persistAll(_inheritance, ctx.db);
      util::persistAll(_imports, ctx.db);
      util::persistAll(_documentations, ctx.db);
      util::persistAll(_types, ctx.db);
    });
}

void Persistence::cppprint(boost::python::object o) {
    std::string s = boost::python::extract<std::string>(o);
    std::cout << s << std::endl;
}

void Persistence::persistFile(boost::python::object pyFile)
{
    try{
        model::FilePtr file = nullptr;
        model::BuildSource buildSource;

        boost::python::object path = pyFile.attr("path");
        boost::python::object status = pyFile.attr("parse_status");

        if(status.is_none()){
            std::cout << "status is None..." << std::endl;
        } else if(path.is_none()){
            std::cout << "path is None..." << std::endl;
        } else {
            file = ctx.srcMgr.getFile(boost::python::extract<std::string>(path));
            file->type = "PY";
            buildSource.file = file;
            switch(boost::python::extract<int>(status)){
                case 0:
                    buildSource.file->parseStatus = model::File::PSNone;
                    break;
                case 1:
                    buildSource.file->parseStatus = model::File::PSPartiallyParsed;
                    break;
                case 2:
                    buildSource.file->parseStatus = model::File::PSFullyParsed;
                    break;
                default:
                    std::cout << "Unknown status: " << boost::python::extract<int>(status) << std::endl;
            }

            model::BuildActionPtr buildAction(new model::BuildAction);
            buildAction->command = "";
            buildAction->type = model::BuildAction::Other;
            try{
                util::OdbTransaction transaction{ctx.db};

                transaction([&, this] {
                    ctx.db->persist(buildAction);
                });

                buildSource.action = buildAction;

                ctx.srcMgr.updateFile(*buildSource.file);

                transaction([&, this] {
                    ctx.db->persist(buildSource);
                });
            } catch(const std::exception& ex){
                std::cout << "Exception: " << ex.what() << " - " << typeid(ex).name() << std::endl;
            }
        }
    } catch(std::exception e){
        std::cout << e.what() << std::endl;
    }
}

void Persistence::persistVariable(boost::python::object pyVariable)
{
    try{
        boost::python::object name = pyVariable.attr("name");
        boost::python::object qualifiedName = pyVariable.attr("qualified_name");
        boost::python::object visibility = pyVariable.attr("visibility");

        boost::optional<model::FileLoc> fileLoc = createFileLocFromPythonFilePosition(pyVariable.attr("file_position"));

        boost::python::list types = boost::python::extract<boost::python::list>(pyVariable.attr("type"));

        boost::python::list usages = boost::python::extract<boost::python::list>(pyVariable.attr("usages"));

        if(name.is_none() || qualifiedName.is_none() || fileLoc == boost::none ||
                types.is_none() || usages.is_none()){
            return;
        }

        model::PythonAstNodePtr varAstNode(new model::PythonAstNode);
        varAstNode->location = fileLoc.get();
        varAstNode->qualifiedName = boost::python::extract<std::string>(qualifiedName);
        varAstNode->symbolType = model::PythonAstNode::SymbolType::Variable;
        varAstNode->astType = model::PythonAstNode::AstType::Declaration;

        varAstNode->id = model::createIdentifier(*varAstNode);

        _astNodes.push_back(varAstNode);
        model::PythonVariablePtr variable(new model::PythonVariable);
        variable->astNodeId = varAstNode->id;
        variable->name = boost::python::extract<std::string>(name);
        variable->qualifiedName = boost::python::extract<std::string>(qualifiedName);
        variable->visibility = boost::python::extract<std::string>(visibility);

        variable->id = model::createIdentifier(*variable);
        _variables.push_back(variable);

        for(int i = 0; i<boost::python::len(types); ++i) {
            model::PythonTypePtr type(new model::PythonType);
            std::string s = boost::python::extract<std::string>(types[i]);
            model::PythonEntityPtr t = getPythonEntity(boost::python::extract<std::string>(types[i]));
            if(t == nullptr){
                continue;
            }
            type->type = t->id;
            type->symbol = variable->id;
            _types.push_back(type);
        }

        _variableUsages[variable->id] = {};
        for(int i = 0; i<boost::python::len(usages); ++i){
            boost::optional<model::FileLoc> usageFileLoc =
                    createFileLocFromPythonFilePosition(usages[i].attr("file_position"));
            if(usageFileLoc == boost::none){
                continue;
            }
            model::PythonAstNodePtr usageAstNode(new model::PythonAstNode);
            usageAstNode->location = usageFileLoc.get();
            usageAstNode->qualifiedName = boost::python::extract<std::string>(qualifiedName);
            usageAstNode->symbolType = model::PythonAstNode::SymbolType::Variable;
            usageAstNode->astType = model::PythonAstNode::AstType::Usage;

            usageAstNode->id = model::createIdentifier(*usageAstNode);

            _variableUsages[variable->id].push_back(usageAstNode);
        }

    } catch (const odb::object_already_persistent& ex)
    {
        std::cout << "Var exception already persistent: " << ex.what() << std::endl;
    } catch (const odb::database_exception& ex)
    {
        std::cout << "Var exception db exception: " << ex.what() << std::endl;
    } catch(std::exception ex){
        std::cout << "Var exception: " << ex.what() << std::endl;
    }
}

void Persistence::persistFunction(boost::python::object pyFunction)
{
    try{
        boost::python::object name = pyFunction.attr("name");
        boost::python::object qualifiedName = pyFunction.attr("qualified_name");
        boost::python::object visibility = pyFunction.attr("visibility");

        boost::optional<model::FileLoc> fileLoc = createFileLocFromPythonFilePosition(pyFunction.attr("file_position"));

        boost::python::list types = boost::python::extract<boost::python::list>(pyFunction.attr("type"));

        boost::python::list usages = boost::python::extract<boost::python::list>(pyFunction.attr("usages"));

        boost::python::object pyDocumentation = pyFunction.attr("documentation");

        boost::python::list params = boost::python::extract<boost::python::list>(pyFunction.attr("parameters"));

        boost::python::list locals = boost::python::extract<boost::python::list>(pyFunction.attr("locals"));

        if(name.is_none() || qualifiedName.is_none() || fileLoc == boost::none || types.is_none() ||
            usages.is_none() || pyDocumentation.is_none() || params.is_none() || locals.is_none()){
            return;
        }

        model::PythonAstNodePtr funcAstNode(new model::PythonAstNode);
        funcAstNode->location = fileLoc.get();
        funcAstNode->qualifiedName = boost::python::extract<std::string>(qualifiedName);
        funcAstNode->symbolType = model::PythonAstNode::SymbolType::Function;
        funcAstNode->astType = model::PythonAstNode::AstType::Declaration;

        funcAstNode->id = model::createIdentifier(*funcAstNode);

        _astNodes.push_back(funcAstNode);

        model::PythonFunctionPtr function(new model::PythonFunction);
        function->astNodeId = funcAstNode->id;
        function->name = boost::python::extract<std::string>(name);
        function->qualifiedName = boost::python::extract<std::string>(qualifiedName);
        function->visibility = boost::python::extract<std::string>(visibility);

        function->id = model::createIdentifier(*function);

        for(int i = 0; i<boost::python::len(params); ++i){
            model::PythonVariablePtr param = getPythonVariable(boost::python::extract<std::string>(params[i]));
            if(param == nullptr){
                continue;
            }
            function->parameters.push_back(param);
        }

        for(int i = 0; i<boost::python::len(locals); ++i){
            model::PythonVariablePtr local = getPythonVariable(boost::python::extract<std::string>(locals[i]));
            if(local == nullptr){
                continue;
            }
            function->locals.push_back(local);
        }

        _functions.push_back(function);

        model::PythonDocumentationPtr documentation(new model::PythonDocumentation);
        documentation->documentation = boost::python::extract<std::string>(pyDocumentation);
        documentation->documented = function->id;
        documentation->documentationKind = model::PythonDocumentation::Function;

        _documentations.push_back(documentation);

        for(int i = 0; i<boost::python::len(types); ++i) {
            model::PythonTypePtr type(new model::PythonType);
            model::PythonEntityPtr t = getPythonEntity(boost::python::extract<std::string>(types[i]));
            if(t == nullptr){
                continue;
            }
            type->type = t->id;
            type->symbol = function->id;

            _types.push_back(type);
        }

        _functionUsages[function->id] = {};
        for(int i = 0; i<boost::python::len(usages); ++i){
            boost::optional<model::FileLoc> usageFileLoc =
                    createFileLocFromPythonFilePosition(usages[i].attr("file_position"));
            if(usageFileLoc == boost::none){
                continue;
            }
            model::PythonAstNodePtr usageAstNode(new model::PythonAstNode);
            usageAstNode->location = usageFileLoc.get();
            usageAstNode->qualifiedName = boost::python::extract<std::string>(qualifiedName);
            usageAstNode->symbolType = model::PythonAstNode::SymbolType::Function;
            usageAstNode->astType = model::PythonAstNode::AstType::Usage;

            usageAstNode->id = model::createIdentifier(*usageAstNode);

            _functionUsages[function->id].push_back(usageAstNode);
        }
    } catch(std::exception e){
        std::cout << "Func exception:" << e.what() << std::endl;
    }
}

void Persistence::persistPreprocessedClass(boost::python::object pyClass)
{
    try{
        boost::python::object name = pyClass.attr("name");
        boost::python::object qualifiedName = pyClass.attr("qualified_name");
        boost::python::object visibility = pyClass.attr("visibility");

        boost::optional<model::FileLoc> fileLoc = createFileLocFromPythonFilePosition(pyClass.attr("file_position"));

        if(name.is_none() || qualifiedName.is_none() || fileLoc == boost::none){
            return;
        }

        model::PythonAstNodePtr classAstNode(new model::PythonAstNode);
        classAstNode->location = fileLoc.get();
        classAstNode->qualifiedName = boost::python::extract<std::string>(qualifiedName);
        classAstNode->symbolType = model::PythonAstNode::SymbolType::Class;
        classAstNode->astType = model::PythonAstNode::AstType::Declaration;

        classAstNode->id = model::createIdentifier(*classAstNode);

        _astNodes.push_back(classAstNode);

        model::PythonClassPtr cl(new model::PythonClass);
        cl->astNodeId = classAstNode->id;
        cl->name = boost::python::extract<std::string>(name);
        cl->qualifiedName = boost::python::extract<std::string>(qualifiedName);
        cl->visibility = boost::python::extract<std::string>(visibility);

        cl->id = model::createIdentifier(*cl);

        _classes.push_back(cl);

    } catch(std::exception e){
        std::cout << "Preprocessed class exception:" << e.what() << std::endl;
    }
}

void Persistence::persistClass(boost::python::object pyClass)
{
    try{
        boost::python::object qualifiedName = pyClass.attr("qualified_name");

        boost::python::list usages = boost::python::extract<boost::python::list>(pyClass.attr("usages"));

        boost::python::object pyDocumentation = pyClass.attr("documentation");

        boost::python::list baseClasses = boost::python::extract<boost::python::list>(pyClass.attr("base_classes"));

        boost::python::object members = pyClass.attr("members");

        if(qualifiedName.is_none() || usages.is_none() || pyDocumentation.is_none() ||
            baseClasses.is_none() || members.is_none()){
            return;
        }

        model::PythonClassPtr cl = getPythonClass(boost::python::extract<std::string>(qualifiedName));

        if (cl == nullptr){
            std::cout << "cl is none" << std::endl;
        }

        model::PythonDocumentationPtr documentation(new model::PythonDocumentation);
        documentation->documentation = boost::python::extract<std::string>(pyDocumentation);
        documentation->documented = cl->id;
        documentation->documentationKind = model::PythonDocumentation::Class;

        _documentations.push_back(documentation);

        _classUsages[cl->id] = {};
        for(int i = 0; i<boost::python::len(usages); ++i){
            boost::optional<model::FileLoc> usageFileLoc =
                    createFileLocFromPythonFilePosition(usages[i].attr("file_position"));
            if(usageFileLoc == boost::none){
                continue;
            }
            model::PythonAstNodePtr usageAstNode(new model::PythonAstNode);
            usageAstNode->location = usageFileLoc.get();
            usageAstNode->qualifiedName = boost::python::extract<std::string>(qualifiedName);
            usageAstNode->symbolType = model::PythonAstNode::SymbolType::Class;
            usageAstNode->astType = model::PythonAstNode::AstType::Usage;

            usageAstNode->id = model::createIdentifier(*usageAstNode);

            _classUsages[cl->id].push_back(usageAstNode);
        }

        for(int i = 0; i<boost::python::len(baseClasses); ++i){
            model::PythonInheritancePtr inheritance(new model::PythonInheritance);
            inheritance->derived = cl->id;
            std::string baseClassQualifiedName = boost::python::extract<std::string>(baseClasses[i]);

            inheritance->base = getPythonEntity(baseClassQualifiedName)->id;

            _inheritance.push_back(inheritance);
        }

        boost::python::list methods = boost::python::extract<boost::python::list>(members.attr("methods"));
        boost::python::list staticMethods = boost::python::extract<boost::python::list>(members.attr("static_methods"));
        boost::python::list attributes = boost::python::extract<boost::python::list>(members.attr("attributes"));
        boost::python::list staticAttributes = boost::python::extract<boost::python::list>(members.attr("static_attributes"));
        boost::python::list classes = boost::python::extract<boost::python::list>(members.attr("classes"));

        if(methods.is_none() || staticMethods.is_none() || attributes.is_none() ||
                staticAttributes.is_none() || classes.is_none()){
            return;
        }

        for(int i = 0; i<boost::python::len(methods); ++i){
            model::PythonClassMemberPtr classMember(new model::PythonClassMember);
            std::string qualifiedName = boost::python::extract<std::string>(methods[i].attr("qualified_name"));
            model::PythonEntityPtr method = getPythonEntity(qualifiedName);
            if(method == nullptr){
                std::cout << "method is none" << std::endl;
            }
            classMember->astNodeId = method->astNodeId;
            classMember->memberId = method->id;
            classMember->classId = cl->id;
            classMember->kind = model::PythonClassMember::Method;
            classMember->staticMember = false;

            _members.push_back(classMember);

            boost::python::list _usages = boost::python::extract<boost::python::list>(methods[i].attr("usages"));
            std::vector<model::PythonAstNodePtr>& _funcUsages = _functionUsages[method->id];
            for(int j = 0; j<boost::python::len(_usages); ++j){
                boost::optional<model::FileLoc> _fl = createFileLocFromPythonFilePosition(_usages[j].attr("file_position"));
                if(_fl == boost::none ||
                    std::find_if(_funcUsages.begin(), _funcUsages.end(), [&](const auto& p){
                        return p->location.file == _fl.get().file && p->location.range == _fl.get().range; }) != _funcUsages.end())
                {
                    continue;
                }
                model::PythonAstNodePtr usageAstNode(new model::PythonAstNode);
                usageAstNode->location = _fl.get();
                usageAstNode->qualifiedName = qualifiedName;
                usageAstNode->symbolType = model::PythonAstNode::SymbolType::Function;
                usageAstNode->astType = model::PythonAstNode::AstType::Usage;

                usageAstNode->id = model::createIdentifier(*usageAstNode);

                _funcUsages.push_back(usageAstNode);
            }
        }

        for(int i = 0; i<boost::python::len(staticMethods); ++i){
            model::PythonClassMemberPtr classMember(new model::PythonClassMember);
            std::string qualifiedName = boost::python::extract<std::string>(staticMethods[i].attr("qualified_name"));
            model::PythonEntityPtr method = getPythonEntity(qualifiedName);
            if(method == nullptr){
                std::cout << "static method is none" << std::endl;
            }
            classMember->astNodeId = method->astNodeId;
            classMember->memberId = method->id;
            classMember->classId = cl->id;
            classMember->kind = model::PythonClassMember::Method;
            classMember->staticMember = true;

            _members.push_back(classMember);

            boost::python::list _usages = boost::python::extract<boost::python::list>(staticMethods[i].attr("usages"));
            std::vector<model::PythonAstNodePtr>& _funcUsages = _functionUsages[method->id];
            for(int j = 0; j<boost::python::len(_usages); ++j){
                boost::optional<model::FileLoc> _fl = createFileLocFromPythonFilePosition(_usages[j].attr("file_position"));
                if(_fl == boost::none ||
                    std::find_if(_funcUsages.begin(), _funcUsages.end(), [&](const auto& p){
                        return p->location.file == _fl.get().file && p->location.range == _fl.get().range; }) != _funcUsages.end())
                {
                    continue;
                }
                model::PythonAstNodePtr usageAstNode(new model::PythonAstNode);
                usageAstNode->location = _fl.get();
                usageAstNode->qualifiedName = qualifiedName;
                usageAstNode->symbolType = model::PythonAstNode::SymbolType::Function;
                usageAstNode->astType = model::PythonAstNode::AstType::Usage;

                usageAstNode->id = model::createIdentifier(*usageAstNode);

                _funcUsages.push_back(usageAstNode);
            }
        }

        for(int i = 0; i<boost::python::len(attributes); ++i){
            model::PythonClassMemberPtr classMember(new model::PythonClassMember);
            std::string qualifiedName = boost::python::extract<std::string>(attributes[i].attr("qualified_name"));
            if (qualifiedName.empty()){
                continue;   // TODO: import symbol in class
            }
            model::PythonEntityPtr attr = getPythonEntity(qualifiedName);
            if(attr == nullptr){
                std::cout << "attr is none" << std::endl;
            }
            classMember->astNodeId = attr->astNodeId;
            classMember->memberId = attr->id;
            classMember->classId = cl->id;
            classMember->kind = model::PythonClassMember::Attribute;
            classMember->staticMember = false;

            _members.push_back(classMember);

            boost::python::list _usages = boost::python::extract<boost::python::list>(attributes[i].attr("usages"));
            std::vector<model::PythonAstNodePtr>& _varUsages = _variableUsages[attr->id];
            for(int j = 0; j<boost::python::len(_usages); ++j){
                boost::optional<model::FileLoc> _fl = createFileLocFromPythonFilePosition(_usages[j].attr("file_position"));
                if(_fl == boost::none ||
                    std::find_if(_varUsages.begin(), _varUsages.end(), [&](const auto& p){
                        return p->location.file == _fl.get().file && p->location.range == _fl.get().range; }) != _varUsages.end())
                {
                    continue;
                }
                model::PythonAstNodePtr usageAstNode(new model::PythonAstNode);
                usageAstNode->location = _fl.get();
                usageAstNode->qualifiedName = qualifiedName;
                usageAstNode->symbolType = model::PythonAstNode::SymbolType::Variable;
                usageAstNode->astType = model::PythonAstNode::AstType::Usage;

                usageAstNode->id = model::createIdentifier(*usageAstNode);

                _varUsages.push_back(usageAstNode);
            }
        }

        for(int i = 0; i<boost::python::len(staticAttributes); ++i){
            model::PythonClassMemberPtr classMember(new model::PythonClassMember);
            std::string qualifiedName = boost::python::extract<std::string>(staticAttributes[i].attr("qualified_name"));
            model::PythonEntityPtr attr = getPythonEntity(qualifiedName);
            if(attr == nullptr){
                std::cout << "static attr is none" << std::endl;
            }
            classMember->astNodeId = attr->astNodeId;
            classMember->memberId = attr->id;
            classMember->classId = cl->id;
            classMember->kind = model::PythonClassMember::Attribute;
            classMember->staticMember = true;

            _members.push_back(classMember);

            boost::python::list _usages = boost::python::extract<boost::python::list>(staticAttributes[i].attr("usages"));
            std::vector<model::PythonAstNodePtr>& _varUsages = _variableUsages[attr->id];
            for(int j = 0; j<boost::python::len(_usages); ++j){
                boost::optional<model::FileLoc> _fl = createFileLocFromPythonFilePosition(_usages[j].attr("file_position"));
                if(_fl == boost::none ||
                    std::find_if(_varUsages.begin(), _varUsages.end(), [&](const auto& p){
                        return p->location.file == _fl.get().file && p->location.range == _fl.get().range; }) != _varUsages.end())
                {
                    continue;
                }
                model::PythonAstNodePtr usageAstNode(new model::PythonAstNode);
                usageAstNode->location = _fl.get();
                usageAstNode->qualifiedName = qualifiedName;
                usageAstNode->symbolType = model::PythonAstNode::SymbolType::Variable;
                usageAstNode->astType = model::PythonAstNode::AstType::Usage;

                usageAstNode->id = model::createIdentifier(*usageAstNode);

                _varUsages.push_back(usageAstNode);
            }
        }

        for(int i = 0; i<boost::python::len(classes); ++i){
            model::PythonClassMemberPtr classMember(new model::PythonClassMember);
            std::string qualifiedName = boost::python::extract<std::string>(classes[i].attr("qualified_name"));
            model::PythonEntityPtr inner = getPythonEntity(qualifiedName);
            if(inner == nullptr){
                std::cout << "inner class is none" << std::endl;
            }
            classMember->astNodeId = inner->astNodeId;
            classMember->memberId = inner->id;
            classMember->classId = cl->id;
            classMember->kind = model::PythonClassMember::Class;
            classMember->staticMember = false;

            _members.push_back(classMember);

            boost::python::list _usages = boost::python::extract<boost::python::list>(classes[i].attr("usages"));
            std::vector<model::PythonAstNodePtr>& _clUsages = _classUsages[cl->id];
            for(int j = 0; j<boost::python::len(_usages); ++j){
                boost::optional<model::FileLoc> _fl = createFileLocFromPythonFilePosition(_usages[j].attr("file_position"));
                if(_fl == boost::none ||
                    std::find_if(_clUsages.begin(), _clUsages.end(), [&](const auto& p){
                        return p->location.file == _fl.get().file && p->location.range == _fl.get().range; }) != _clUsages.end())
                {
                    continue;
                }
                model::PythonAstNodePtr usageAstNode(new model::PythonAstNode);
                usageAstNode->location = _fl.get();
                usageAstNode->qualifiedName = qualifiedName;
                usageAstNode->symbolType = model::PythonAstNode::SymbolType::Class;
                usageAstNode->astType = model::PythonAstNode::AstType::Usage;

                usageAstNode->id = model::createIdentifier(*usageAstNode);

                _clUsages.push_back(usageAstNode);
            }
        }
    } catch(std::exception e){
        std::cout << "Class exception:" << e.what() << std::endl;
    }
}

void Persistence::persistImport(boost::python::object pyImport)
{
    try {
        model::FilePtr file = ctx.srcMgr.getFile(boost::python::extract<std::string>(pyImport.attr("importer")));

        boost::python::list importedModules =
                boost::python::extract<boost::python::list>(pyImport.attr("imported_modules"));

        boost::python::dict importedSymbols =
                boost::python::extract<boost::python::dict>(pyImport.attr("imported_symbols"));

        if (file == nullptr || importedModules.is_none() || importedSymbols.is_none()) {
            return;
        }

        for (int i = 0; i < boost::python::len(importedModules); ++i) {
            boost::python::object importData = importedModules[i];

            model::FilePtr moduleFile = ctx.srcMgr.getFile(boost::python::extract<std::string>(importData.attr("imported")));
            boost::optional<model::FileLoc> fileLoc = createFileLocFromPythonFilePosition(importData.attr("position"));

            if (moduleFile == nullptr || fileLoc == boost::none) {
                continue;
            }

            model::PythonAstNodePtr moduleAstNode(new model::PythonAstNode);
            moduleAstNode->location = fileLoc.get();
            moduleAstNode->qualifiedName = "";
            moduleAstNode->symbolType = model::PythonAstNode::SymbolType::Module;
            moduleAstNode->astType = model::PythonAstNode::AstType::Declaration;

            moduleAstNode->id = model::createIdentifier(*moduleAstNode);

            model::PythonImportPtr moduleImport(new model::PythonImport);
            moduleImport->astNodeId = moduleAstNode->id;
            moduleImport->importer = file;
            moduleImport->imported = moduleFile;

            _astNodes.push_back(moduleAstNode);
            _imports.push_back(moduleImport);
        }

        boost::python::list importDict = importedSymbols.items();
        for (int i = 0; i < boost::python::len(importDict); ++i) {
            boost::python::tuple import = boost::python::extract<boost::python::tuple>(importDict[i]);

            boost::python::object importData = import[0];

            model::FilePtr moduleFile = ctx.srcMgr.getFile(boost::python::extract<std::string>(importData.attr("imported")));
            boost::optional<model::FileLoc> fileLoc = createFileLocFromPythonFilePosition(importData.attr("position"));

            if (moduleFile == nullptr || fileLoc == boost::none) {
                continue;
            }

            model::PythonAstNodePtr moduleAstNode(new model::PythonAstNode);
            moduleAstNode->location = fileLoc.get();
            moduleAstNode->qualifiedName = "";
            moduleAstNode->symbolType = model::PythonAstNode::SymbolType::Module;
            moduleAstNode->astType = model::PythonAstNode::AstType::Declaration;

            moduleAstNode->id = model::createIdentifier(*moduleAstNode);

            for (int j = 0; j < boost::python::len(import[1]); ++j){
                model::PythonImportPtr moduleImport(new model::PythonImport);
                moduleImport->astNodeId = moduleAstNode->id;
                moduleImport->importer = file;
                moduleImport->imported = moduleFile;
                auto symb = getPythonEntity(boost::python::extract<std::string>(import[1][j]));
                moduleImport->importedSymbol = symb->id;

                _imports.push_back(moduleImport);
            }

            _astNodes.push_back(moduleAstNode);
        }
    } catch(std::exception e){
        std::cout << "Import exception:" << e.what() << std::endl;
    }
}

boost::optional<model::FileLoc> Persistence::createFileLocFromPythonFilePosition(boost::python::object filePosition)
{
    if (filePosition.is_none()){
        return boost::none;
    }

    boost::python::object filePath = filePosition.attr("file");
    boost::python::object pyRange = filePosition.attr("range");

    if (filePath.is_none() || pyRange.is_none()){
        return boost::none;
    }

    boost::python::object pyStartPosition = pyRange.attr("start_position");
    boost::python::object pyEndPosition = pyRange.attr("end_position");

    if (pyStartPosition.is_none() || pyEndPosition.is_none()){
        return boost::none;
    }

    model::FileLoc fileLoc;

    fileLoc.file = ctx.srcMgr.getFile(boost::python::extract<std::string>(filePath));

    model::Position startPosition(boost::python::extract<int>(pyStartPosition.attr("line")),
                                boost::python::extract<int>(pyStartPosition.attr("column")));
    model::Position endPosition(boost::python::extract<int>(pyEndPosition.attr("line")),
                                boost::python::extract<int>(pyEndPosition.attr("column")));

    fileLoc.range = model::Range(startPosition, endPosition);

    return fileLoc;
}

model::PythonEntityPtr Persistence::getPythonEntity(const std::string& qualifiedName)
{
    if(qualifiedName.empty()){
        return nullptr;
    }

    auto varIt = std::find_if(_variables.begin(), _variables.end(), [&](const auto& var){ return var->qualifiedName == qualifiedName; });
    if (varIt != _variables.end()){
        return *varIt;
    }

    auto funcIt = std::find_if(_functions.begin(), _functions.end(), [&](const auto& func){ return func->qualifiedName == qualifiedName; });
    if (funcIt != _functions.end()){
        return *funcIt;
    }

    auto classIt = std::find_if(_classes.begin(), _classes.end(), [&](const auto& cl){ return cl->qualifiedName == qualifiedName; });
    if (classIt != _classes.end()){
        return *classIt;
    }

    return nullptr;
}

model::PythonVariablePtr Persistence::getPythonVariable(const std::string& qualifiedName)
{
    if(qualifiedName.empty()){
        return nullptr;
    }

    auto varIt = std::find_if(_variables.begin(), _variables.end(), [&](const auto& var){ return var->qualifiedName == qualifiedName; });
    if (varIt != _variables.end()){
        return *varIt;
    }

    return nullptr;
}

model::PythonClassPtr Persistence::getPythonClass(const std::string& qualifiedName)
{
    auto classIt = std::find_if(_classes.begin(), _classes.end(), [&](const auto& cl){ return cl->qualifiedName == qualifiedName; });
    if (classIt != _classes.end()){
        return *classIt;
    }

    return nullptr;
}

typedef boost::shared_ptr<Persistence> PersistencePtr;

BOOST_PYTHON_MODULE(persistence){
    boost::python::class_<Persistence>("Persistence", boost::python::init<ParserContext&>())
            .def("print", &Persistence::cppprint)
            .def("persist_file", &Persistence::persistFile)
            .def("persist_variable", &Persistence::persistVariable)
            .def("persist_function", &Persistence::persistFunction)
            .def("persist_preprocessed_class", &Persistence::persistPreprocessedClass)
            .def("persist_class", &Persistence::persistClass)
            .def("persist_import", &Persistence::persistImport);
}

PythonParser::PythonParser(ParserContext &ctx_) : AbstractParser(ctx_) {}

PythonParser::~PythonParser()
{
}

void PythonParser::markModifiedFiles() {}

bool PythonParser::cleanupDatabase() { return true; }

bool PythonParser::parse()
{
    const std::string PARSER_SCRIPTS_DIR = _ctx.compassRoot + "/lib/parserplugin/scripts/python";
    if (!boost::filesystem::exists(PARSER_SCRIPTS_DIR) || !boost::filesystem::is_directory(PARSER_SCRIPTS_DIR)){
        throw std::runtime_error(PARSER_SCRIPTS_DIR + " is not a directory!");
    }

    setenv("PYTHONPATH", PARSER_SCRIPTS_DIR.c_str(), 1);

    try{
        Py_Initialize();
        init_module_persistence();

        boost::python::object module = boost::python::import("cc_python_parser.python_parser");

        if(!module.is_none()){
            boost::python::object func = module.attr("parse");

            if(!func.is_none() && PyCallable_Check(func.ptr())){
                std::string source_path;
                for (const std::string& input : _ctx.options["input"].as<std::vector<std::string>>()){
                    if (boost::filesystem::is_directory(input)){
                        source_path = input;
                    }
                }
                if(source_path.empty()){
                    std::cout << "No source path was found" << std::endl;
                } else {
                    PersistencePtr persistencePtr(new Persistence(_ctx));

                    func(source_path, boost::python::ptr(persistencePtr.get()));
                }
            } else {
                std::cout << "Cannot find function" << std::endl;
            }
        } else {
            std::cout << "Cannot import module" << std::endl;
        }
    }catch(boost::python::error_already_set){
        PyErr_Print();
    }

    // Py_Finalize();
    return true;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
extern "C"
{
boost::program_options::options_description getOptions()
{
    boost::program_options::options_description description("Python Plugin");
    description.add_options()
            ("skip-doccomment",
             "If this flag is given the parser will skip parsing the documentation "
             "comments.");
    return description;
}

std::shared_ptr<PythonParser> make(ParserContext& ctx_)
{
    return std::make_shared<PythonParser>(ctx_);
}
}
#pragma clang diagnostic pop

}
}
