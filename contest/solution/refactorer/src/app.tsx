import { onCleanup, createSignal, createEffect, createMemo, For, onMount } from "solid-js";
import { editor, languages } from 'monaco-editor';
import { get as dbGet, set as dbSet } from 'idb-keyval';


import "./app.css";
import { myCppRules } from "./myCpp_rules";

import classProperties from "./classProperties";
import annotatedProperties from "./annotatedProperties";
import { stringHash } from "./various";


type ObjDict<T> = { [key: string]: T };

type MyFile = {
  name: string,
  handle: FileSystemFileHandle,
  code: string,
  lines: string[],
};

type FunctionInfo = {
  name: string,
  file: MyFile,
  startLine: number,
  endLine: number,
  code: string,
  editedCode: string,
};

type CallGraphNode = {
  f: FunctionInfo,
  x: number,
  y: number,
};

type LanguageVariant = {
  name: string,
  definition: languages.IMonarchLanguage,
}


const myCppTheme = 'myCppTheme';
editor.defineTheme(myCppTheme, {
  base: 'vs',
  inherit: true,
  colors: {
    'editor.foreground': '#000000',
  },
  rules: [
    { token: 'targetProperty', foreground: 'ff0000', fontStyle: 'bold underline' },
    { token: 'targetFunction', foreground: '000000', fontStyle: 'bold underline' },
  ]
});
// ^ https://stackoverflow.com/questions/52700307/how-to-use-monaco-editor-for-syntax-highlighting : https://stackoverflow.com/a/63219877/7788315


const cache: ObjDict<LanguageVariant> = {};

function languageVariant(targetProperty: string, functionNames: string[], propsToHighlight: string[]) {
  const key = targetProperty + '___' + functionNames.toSorted().join(',') + '___' + propsToHighlight.toSorted().join(',');

  if (!cache[key]) {
    const res = {
      name: 'myCpp_' + key,
      definition: {
        ...myCppRules.language,
        toHighlight: [key],
        functionsToHighlight: functionNames,
        propertiesToHighlight: propsToHighlight,
      },
    };
    cache[key] = res;

    languages.register({ id: res.name });
    languages.setMonarchTokensProvider(res.name, res.definition);

  }
  
  return cache[key];
}


async function createFileGroup(dbKey: string) {
  async function loadedFiles(): Promise<[boolean, MyFile[]]> {
    const loadedPartialFiles = await dbGet(dbKey);
    if (!loadedPartialFiles) {
      return [true, []];
    }
  
    const newFiles = [];
    for (const { handle } of loadedPartialFiles) {
      if (await handle.queryPermission({ mode: 'readwrite' }) !== 'granted') {
        return [false, []];
      }
      const fileObj = await handle.getFile();
      console.log('file:', fileObj);
  
      const code = await fileObj.text();
      const lines = code.split('\n');
      
      newFiles.push({
        name: fileObj.name,
        handle,
        code,
        lines,
      });
    }
  
    return [true, newFiles];
  }
  const __initialState = await loadedFiles();

  const [errorAutoLoading, setErrorAutoLoading] = createSignal(!__initialState[0]);
  const [files, setFilesPrivate] = createSignal<MyFile[]>(__initialState[1]);

  function setFiles(newFiles: MyFile[]) {
    setErrorAutoLoading(false);
    setFilesPrivate(newFiles);
    dbSet(dbKey, newFiles.map(f => {
      const { handle } = f;
      return { handle };
    }));
  }

  async function openFiles() {
    const handles = await untypedWindow.showOpenFilePicker!({ multiple: true });
  
    const newFiles = [];
    for (const handle of handles) {
      const fileObj = await handle.getFile();
      console.log('file:', fileObj);
  
      const code = await fileObj.text();
      const lines = code.split('\n');
      
      newFiles.push({
        name: fileObj.name,
        handle,
        code,
        lines,
      });
    }
  
    patchingInfo = {};
    setFiles([...files(), ...newFiles]);
  };  

  async function loadFiles() {
    const loadedPartialFiles = await dbGet(dbKey);
    if (!loadedPartialFiles) {
      alert('Nothing to load');
      return;
    }
  
    const newFiles = [];
    for (const { handle } of loadedPartialFiles) {
      await handle.requestPermission({ mode: 'readwrite' });
      const fileObj = await handle.getFile();
      console.log('file:', fileObj);
  
      const code = await fileObj.text();
      const lines = code.split('\n');
      
      newFiles.push({
        name: fileObj.name,
        handle,
        code,
        lines,
      });
    }
  
    setFilesPrivate(newFiles);
  }
  
  
  async function reloadFiles() {
    const nextFiles = [];
  
    for (const file of files()) {
      const fileObj = await file.handle.getFile();
      const code = await fileObj.text();
      const lines = code.split('\n');
  
      nextFiles.push(code === file.code ? file : {
        ...file,
        code,
        lines,
      });
    }
  
    patchingInfo = {};
    setFiles(nextFiles);
  };
  
  
  return {
    files, setFiles, openFiles,
    loadFiles, reloadFiles,
    errorAutoLoading,
  };
}
type FileGroup = Awaited<ReturnType<typeof createFileGroup>>;


async function writeToFile(handle: FileSystemFileHandle, text: string) {
  const writable = await handle.createWritable();
  await writable.write(text);
  await writable.close();
}



const propertiesToHighlightStringKey = 'propertiesToHighlightString';
async function setPropertiesToHighlightString(value: string) {
  await dbSet(propertiesToHighlightStringKey, value);
  setPropertiesToHighlightStringPrivate(value);
}
const __initialPropertiesToHighlightString = await dbGet(propertiesToHighlightStringKey) ?? '';


const functionsToDAGKey = 'functionsToDAG';
async function setFunctionsToDAGString(value: string) {
  await dbSet(functionsToDAGKey, value);
  setFunctionsToDAGStringPrivate(value);
}
const __initialFunctionsToDAG = await dbGet(functionsToDAGKey) ?? '';


const mFs = await createFileGroup('files');
const spFs = await createFileGroup('special_files');

const [allFunctions, setAllFunctions] = createSignal<FunctionInfo[]>([]);
const [property, setProperty] = createSignal('');
const [selectedFunction, setSelectedFunction] = createSignal<null | FunctionInfo>(null);
const [modifiedFunctions, setModifiedFunctions] = createSignal<FunctionInfo[]>([]);
const [propertiesToHighlightString, setPropertiesToHighlightStringPrivate] = createSignal<string>(__initialPropertiesToHighlightString);
const [functionsToDAGString, setFunctionsToDAGStringPrivate] = createSignal<string>(__initialFunctionsToDAG);


// const atomicZeroChecks = createSpecialFileStuff('generated_atomic_zero_checks')
// const atomicVars = createSpecialFileStuff('generated_atomic_vars')
// const dagRoot = createSpecialFileStuff('generated_dag_root')

const propertyType = () => property().split(' ').slice(0, -1).join(' ').trim();
const propertyName = () => property().split(' ').at(-1);
const propertyValid = () => (propertyType() && propertyName() && true);

const propertiesToHighlight = createMemo(() => propertiesToHighlightString().split('\n').map(s => s.trim()).filter(s => s));
function containsPropertyToHighlight(func: FunctionInfo) {
  return propertiesToHighlight().some(prop => regexForName(prop).test(func.code));
}

/* {
  name: '',
  handle: null,
  code: '',
  lines: [],
} */
let $property = null;



type PatchingInfo = {
  func: FunctionInfo,
  originalModel: editor.ITextModel,
  modifiedModel: editor.ITextModel,
};
let patchingInfo: ObjDict<PatchingInfo[]> = {};

// let handle = null;

let untypedWindow = window as any;











async function patch() {
  let patchedSomething = false;

  const nextFiles = [];

  for (const file of mFs.files()) {
    const info = patchingInfo[file.name];
    if (!info) {
      nextFiles.push(file);
      continue;
    }

    let patchedCode = '';

    let prevEndLine = 0;
    for (const { func, originalModel, modifiedModel } of info) {
      patchedCode += file.lines.slice(prevEndLine, func.startLine).join('\n');
      const modifiedCode = modifiedModel.getValue();
      patchedCode += '\n' + modifiedCode + '\n';

      prevEndLine = func.endLine;
    }
    const rest = file.lines.slice(prevEndLine).join('\n');
    patchedCode += rest;
    
    if (patchedCode !== file.code) {
      await writeToFile(file.handle, patchedCode);

      // console.log(patchedCode);
      // window.patchedCode = patchedCode;
      patchedSomething = true;

      // for (const { func, originalModel, modifiedModel } of info) {
      // 	modifiedModel.setValue(originalModel.getValue());
      // }

      // break;
    }

    nextFiles.push(patchedCode === file.code ? file : {
      ...file,
      code: patchedCode,
      lines: patchedCode.split('\n'),
    });
  }

  if (!patchedSomething) {
    alert('Nothing patched');
  }
  console.log('patchedSomething:', patchedSomething);

  mFs.setFiles(nextFiles);
};



const launcherFunction = 'my_threader.launch';
// my_threader.launch([this] { precheck_message_queue_update(); })
const launchStringOf = (fname: string) => `${launcherFunction}([this] { ${functionCallString()[fname] } })`;

async function patchEdited() {
  let patchedSomething = false;
  const nextFiles = [];

  for (const file of mFs.files()) {
    let patchedCode = '';

    const fileFuncs = allFunctions().filter(f => f.file === file); // Assume they are sorted
    
    let prevEndLine = 0;
    for (const func of fileFuncs) {
      patchedCode += file.lines.slice(prevEndLine, func.startLine).join('\n');
      patchedCode += '\n' + func.editedCode + '\n';

      prevEndLine = func.endLine;
    }
    const rest = file.lines.slice(prevEndLine).join('\n');
    patchedCode += rest;
    
    if (patchedCode !== file.code) {
      await writeToFile(file.handle, patchedCode);

      // console.log(patchedCode);
      // window.patchedCode = patchedCode;
      patchedSomething = true;

      // for (const { func, originalModel, modifiedModel } of info) {
      // 	modifiedModel.setValue(originalModel.getValue());
      // }

      // break;
    }

    nextFiles.push(patchedCode === file.code ? file : {
      ...file,
      code: patchedCode,
      lines: patchedCode.split('\n'),
    });
  }

  const insertions = {
    'generated_dag_root': generatedDagRootString,
    'generated_atomic_vars': generatedAtomicVarsString,
    'generated_atomic_zero_checks': generatedAtomicZeroChecksString,
  };
  // <{generated_dag_root
  //     generated content
	// generated_dag_root}/>
  const insertionsCompleted: ObjDict<boolean> = {};

  const nextSpecialFiles = [];
  for (const file of spFs.files()) {
    let patchedCode = file.code;

    for (const [key, value] of Object.entries(insertions)) {
      if (value === null)
        continue;

      patchedCode = patchedCode.replaceAll(
        new RegExp(`(?<openLine>[ \\t]*//[ \\t]*<\\{${key}[ \\t]*\\n).*?(?<closeLine>[ \\t]*//[ \\t]*${key}\\}/>)`, 'gs'),
        (...args) => {
          if (insertionsCompleted[key]) {
            const errorMessage = `Already completed a generated substitution for "${key}". Is there a duplicate?`;
            alert(errorMessage);
            throw errorMessage;
          }
          insertionsCompleted[key] = true;

          const groups = args.at(-1);
          const { openLine, closeLine } = groups;
  
          return openLine + value + closeLine;
        }
      );
    }

    if (patchedCode !== file.code) {
      await writeToFile(file.handle, patchedCode);
      patchedSomething = true;
    }

    nextSpecialFiles.push(patchedCode === file.code ? file : {
      ...file,
      code: patchedCode,
      lines: patchedCode.split('\n'),
    });
  }

  for (const [key, value] of Object.entries(insertions))
    if (value !== null && !insertionsCompleted[key]) {
      const errorMessage = `Expected to make a substitution for "${key}", but no appropriate marks found in special files.`
      alert(errorMessage);
      throw errorMessage;
    }

  if (!patchedSomething) {
    alert('Nothing patched');
    return;
  }
  console.log('patchedSomething');

  mFs.setFiles(nextFiles);
  spFs.setFiles(nextSpecialFiles);
}



const functionsToDAGWithInfo = createMemo(() => {
  const functionCallsToDAG = functionsToDAGString().split('\n').map(s => s.trim()).filter(s => s);
  let functionNamesToDAG = [];
  let functionCallString: ObjDict<string> = {};
  for (let functionLine of functionCallsToDAG) {
    let openBracketIndex = functionLine.indexOf('(');
    let functionName = openBracketIndex < 0 ? functionLine : functionLine.slice(0, openBracketIndex).trim();
    functionNamesToDAG.push(functionName);

    if (!functionLine.includes('('))
      functionLine += '()';
    if (!functionLine.endsWith(';'))
      functionLine += ';';
    functionCallString[functionName] = functionLine;
  }

  const notFoundFunctions: string[] = [];
  const functions: FunctionInfo[] = [];
  
  for (const name of functionNamesToDAG) {
    const func = allFunctions().find(f => f.name === name);
    if (func)
      functions.push(func);
    else
      notFoundFunctions.push(name);
  }

  return { notFoundFunctions, functions, functionCallString };
});

const functionsToDAG = createMemo(() => functionsToDAGWithInfo().functions);
const functionCallString = createMemo(() => functionsToDAGWithInfo().functionCallString);

function dagFunctions() {
  try {
    const {
      notFoundFunctions,
      functions: _functionsToDAG,
    } = functionsToDAGWithInfo();

    if (notFoundFunctions.length > 0) {
      alert(`Couldn't find a function with name ${name}`);
      console.log('notFoundFunctions:', notFoundFunctions);
      return;
    }
  
    const pendingVariables: ObjDict<number> = {};
    const pendingVariableOf_inc = (fname: string) => {
      const varName = '__pending_' + fname;
      pendingVariables[varName] = (pendingVariables[varName] ?? 0) + 1;
      return varName;
    }
  
    console.log(_functionsToDAG);
  
    const { calling } = calls();
  
  
    const relevantFuncs = new Set<FunctionInfo>();
    const allProps = new Set<string>();
    const funcPropMentions: ObjDict<Set<string>> = {};
    const propMentionedIn_root: ObjDict<Set<string>> = {};
    const funcAssigns: ObjDict<Set<string>> = {};
    const assignedIn: ObjDict<FunctionInfo> = {};
    const assignedIn_root: ObjDict<FunctionInfo> = {};
  
    const dependentOn = (prop: string) => [...propMentionedIn_root[prop]].filter(f => f !== assignedIn_root[prop].name);
    const dependenciesOf = (func: FunctionInfo) => [...funcPropMentions[func.name]].filter(prop => assignedIn_root[prop] !== func)
  
    const generatedStartMark = `//<%generated%>`;
    const generatedEndMark = `//<%/generated%>`;
  
    const assignREall = new RegExp(`//\\s*<%assigned%>\\s*:\\s*(?<prop>[\\w\\.]+)[ \\t]*(?<final>\\!?).*`, 'g'); // .* at the end to match any comments
    // This one ^ can be made to match dots, because it's ok to match until the end
    const generatedRE = new RegExp(`[ \\t]*${generatedStartMark}.*?${generatedEndMark}[ \\t]*\\n`, 'gs');
    const replaceUsageRE = new RegExp(`//\\s*<%replace_usage%>\\s*:\\s*(?<from>[\\w\\.]+)\\s*->\\s*(?<to>[\\w\\.]+)`, 'g');

  
    function dfs(root: FunctionInfo, node: FunctionInfo, propertyReplacements: Map<string, string>) {
      relevantFuncs.add(node);

      // if (node.code.includes('<%replace_usage%>')) {
      //   alert(`Still gotta code <%replace_usage%> part`);
      //   throw `<%replace_usage%> not implemented`;
      // }

      // ----- Account for <%replace_usage%> -----
      const replacementMatches = [...node.code.matchAll(replaceUsageRE)];
      if (node !== root && replacementMatches.length > 0) {
        const errorMessage = `Replacement declarations are only allowed at DAG root functions,\nbut some declaration(s) found in ${node.name}:\n${replacementMatches.map(m => m[0]).join('\n')}`;
        alert(errorMessage);
        throw replacementMatches.map(m => m[0]);
      }

      for (const m of replacementMatches) {
        const { from, to } = m.groups!;
        propertyReplacements.set(from, to);
      }
  
      // ----- Find all classProperties mentions -----
      for (let classProp of classProperties) {
        const re = regexForName(classProp);
        if (!re.test(node.code))
          continue;

        if (propertyReplacements.has(classProp)) {
          classProp = propertyReplacements.get(classProp)!;
        }
  
        allProps.add(classProp);
        funcPropMentions[root.name] = (funcPropMentions[root.name] ?? new Set()).add(classProp);
        propMentionedIn_root[classProp] = (propMentionedIn_root[classProp] ?? new Set()).add(root.name);
  
  
        // const reAssign = regexForAssignedName(classProp);
        // if (!reAssign.test(node.code))
        //   continue;
        // const prop = classProp;
      }

      // ----- Account for <%assigned%> -----
      const assignedMatches = node.code.matchAll(assignREall);
      for (const match of assignedMatches) {
        const prop = match.groups!.prop;

        // The following check was for when a RegExp was matching everything (but that was removed due to necessity to match ns_.something_)
        // Edit to ^: I reverted it back, but it's still commented out, because with replace_usage everything can be theoretically assigned
        // if (!classProperties.includes(prop)) {
        //   const errorMessage = `Unknown property found in <%assigned%> annotation: ${prop}`;
        //   alert(errorMessage);
        //   throw errorMessage;
        // }

        if (assignedIn[prop] && assignedIn[prop] !== node) {
          const errorMessage = `Property ${prop} has been marked "assigned" previously in ${assignedIn[prop].name}\nand now also marked "assigned" in ${node.name}`;
          alert(errorMessage);
          throw errorMessage;
        }

        assignedIn[prop] = node;
        assignedIn_root[prop] = root;
        funcAssigns[root.name] = (funcAssigns[root.name] ?? new Set()).add(prop);
      }
  
  
      for (const child of calling[node.name] ?? []) {
        if (_functionsToDAG.includes(child))
          continue;
        // All functionsToDAG are considered top level and independent from each other
        // The reason one might end up a "child" of another is because the code-generated parts
        // are also taken into account when initially looking for "children"

        dfs(root, child, propertyReplacements);
      }
    }
  
    for (const func of _functionsToDAG) {
      dfs(func, func, new Map());
    }
  
    const unAnnotatedProperties = [...allProps.values().filter(dep => !annotatedProperties.includes(dep))];
    if (unAnnotatedProperties.length > 0) {
      const errorMessage = `There are ${unAnnotatedProperties.length} properties, which are mentioned in functionsToDAG, but are not annotated:\n`;
      alert(errorMessage + unAnnotatedProperties.join('\n'));
      console.error(errorMessage, unAnnotatedProperties);
      return;
    }
  
      
    let modifiedSomething = false;
    const nextModifiedFunctions = [...modifiedFunctions()];
    for (const func of relevantFuncs) {
      let nextCode = func.code.replaceAll(generatedRE, ''); // Clean up previous generation
      nextCode = nextCode.replaceAll(assignREall, (matchedString, ...args) => {
        console.log('matchedString:', matchedString);
        // ...args, because the replacement function behaves stupidly inconsistently with parameter count:
        // https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/String/replace
        const groups = args.at(-1);
        const metaProp = groups.prop; // Might not be a classProp, might be one of the "replaced" ones
        const final = groups.final.length > 0;

        if (final) {
          if (propMentionedIn_root[metaProp] !== undefined) {
            const errorMessage = `Property ${metaProp} is marked as "!"final, but is mentioned in ${propMentionedIn_root[metaProp].size} functions:\n`;
            const mentions = [...propMentionedIn_root[metaProp]];
            alert(errorMessage + mentions.join('\n'));
            console.error(errorMessage, mentions);
            throw errorMessage;
          }
        }
  
        const indent = `\t\t\t\t\t\t\t`;
  
        return (
          `${matchedString}\n`
          + `${indent}${generatedStartMark}\n`
          + (final ? '' : dependentOn(metaProp).map(fname => `${indent}if (--${pendingVariableOf_inc(fname)} == 0) ${launchStringOf(fname)};\n`).join(''))
          + `${indent}${generatedEndMark}`
        );
      });
  
      if (nextCode !== func.editedCode && func === selectedFunction()) {
        mdModifiedModel.setValue(nextCode);
      }
  
      func.editedCode = nextCode;
      if (func.editedCode !== func.code) {
        modifiedSomething = true;
        if (!nextModifiedFunctions.includes(func))
          nextModifiedFunctions.push(func);
      } else {
        // We could theoretically "unmodify" something
        // but this entire code only works on .code (ignoring .editedCode) anyway
        // So not worth thinking too much about it
      }
    }
    // Clean up the rest of functions
    for (const func of allFunctions()) {
      if (relevantFuncs.has(func))
        continue;
  
      func.editedCode = func.code.replaceAll(generatedRE, '');
      if (func.editedCode !== func.code) {
        if (func === selectedFunction())
          mdModifiedModel.setValue(func.editedCode);
  
        modifiedSomething = true;
        if (!nextModifiedFunctions.includes(func))
          nextModifiedFunctions.push(func);
      }
    }
  
    if (!modifiedSomething) {
      const warningMessage = `Somehow, no functions are modified`;
      alert(warningMessage);
      console.warn(warningMessage);
    } else {
      setModifiedFunctions(nextModifiedFunctions);
    }
    
  
  
    const topLevelFunctions: FunctionInfo[] = [];
  
    for (const func of _functionsToDAG) {
      let topLevel = true; // Might have dependencies, but no dependencies from functions we are DAGing
      for (const prop of dependenciesOf(func)) {
        if (assignedIn_root[prop]) {
          topLevel = false;
          break;
        }
      }
  
      if (topLevel)
        topLevelFunctions.push(func);
    }
  
    if (topLevelFunctions.length == 0) {
      const errorMessage = `There are no topLevelFunctions detected, circular dependencies?`;
      alert(errorMessage);
      throw errorMessage;
    }
  
    console.log('topLevelFunctions:', topLevelFunctions);
    generatedDagRootString = topLevelFunctions.map(f => '  ' + launchStringOf(f.name) + ';\n').join('');
    console.log('topLevelString:\n\n' + generatedDagRootString);

    console.log('pendingVariables:', pendingVariables);
    generatedAtomicVarsString = Object.entries(pendingVariables)
      .map(([varName, varCount]) => `  std::atomic<int> ${varName}{${varCount}};\n`).join('');
    console.log('pendingVariablesString:\n\n' + generatedAtomicVarsString);

    generatedAtomicZeroChecksString = Object.keys(pendingVariables)
      .map(varName => `  if (${varName} != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable ${varName} ended up as: " << ${varName};\n`).join('');
    console.log('pendingVariableCheckString:\n\n' + generatedAtomicZeroChecksString);
  } catch (error) {
    console.error(error);
  }
}
let generatedDagRootString: string | null = null;
let generatedAtomicVarsString: string | null = null;
let generatedAtomicZeroChecksString: string | null = null;


createEffect(function extractFunctionsFromFiles() {
  let functions: FunctionInfo[] = [];
  // {
    // 	name,
    //	file,
    // 	startLine,
    // 	endLine,
  // };

  for (const file of mFs.files()) {
    let lastFunction: FunctionInfo | null = null;
    let lineI = 0;
    for (let line of file.lines) {
      const functionRegex = /ContestValidateQuery::(?<name>[^\(]+)\(/;
      const functionEndRegex = /^}\s*$/;

      const match = line.match(functionRegex);
      if (match && !/\s/.test(line[0])) { // Trust that function definitions don't start with whitespace
        if (lastFunction) {
          alert('Function without end found:' + lastFunction.name);
          console.error('Function without end found:', lastFunction);
        }
        lastFunction = {
          file,
          name: match.groups!.name,
          startLine: lineI,
        } as FunctionInfo;
        functions.push(lastFunction);
      }
      if (functionEndRegex.test(line)) {
        if (lastFunction) { // Because functions which are not ContestValidateQuery:: are not included, but there end is found
          lastFunction.endLine = lineI + 1;
          lastFunction.editedCode = lastFunction.code = file.lines.slice(lastFunction.startLine, lastFunction.endLine).join('\n');
          lastFunction = null;
        }
      }

      lineI++;
    }

    if (lastFunction) {
      alert('Function without end found:' + lastFunction.name);
      console.error('Function without end found:', lastFunction);
    }
  }
  
  const badFuncs = functions.filter(func => !func.endLine);
  if (badFuncs.length > 0) {
    alert('Some functions are missing an end line, list in the console');
    console.error('Bad functions:', badFuncs);
  }

  const overloadCount: ObjDict<number> = {};
  for (const func of functions) {
    overloadCount[func.name] = (overloadCount[func.name] ?? 0) + 1;
  }
  functions = functions.filter(func => overloadCount[func.name] === 1 && func.name !== 'ContestValidateQuery');
  // Remove overloaded functions: those are functions like reject_throw, fatal_throw, etc.
  // They don't help to understand the big picture
  
  setAllFunctions(functions);
});

function analyzeProperty(name: string) {
  return allFunctions().filter(func => func.code.includes(name));
}

const regexForName = (name: string) => new RegExp(`\\b${name.replaceAll('.', '\\.')}\\b`, 'g');
const regexForAssignedName = (name: string) => new RegExp(`//\\s*<%assigned%>\\s*:\\s*${name.replaceAll('.', '\\.')}\\b`, 'g');
/* Will match (for specific names):
  //    <%assigned%>:    zhuk
  //<%assigned%>:    Lak  
  //  <%assigned%> : Vra
*/


const calls = createMemo(() => {
  const functions = allFunctions();
  const calling: ObjDict<FunctionInfo[]> = {};
  const calledFrom: ObjDict<FunctionInfo[]> = {};

  // The following code worked, but produced "incorrect" calling ordering (not in the order of first appearance) 
  // for (const inner of functions) {
  //   const regex = regexForName(inner.name);
  //   for (const outer of functions) {
  //     if (outer !== inner && regex.test(outer.code)) {
  //       calling[outer.name] = [...(calling[outer.name] ?? []), inner];
  //       calledFrom[inner.name] = [...(calledFrom[inner.name] ?? []), outer];
  //     }
  //   }
  // }

  for (const outer of functions) {
    for (const m of outer.code.matchAll(/\w+/g)) {
      const name = m[0];

      const inner = functions.find(f => f.name === name);
      if (!inner || inner === outer)
        continue;

      calling[outer.name] = calling[outer.name] ?? [];
      if (!calling[outer.name].includes(inner))
        calling[outer.name].push(inner);

      calledFrom[inner.name] = calledFrom[inner.name] ?? [];
      if (!calledFrom[inner.name].includes(outer))
        calledFrom[inner.name].push(outer);
    }
  }

  return { calling, calledFrom };
});

const roots = createMemo(() => allFunctions().filter(func => !calls().calledFrom[func.name]));

const circularDependencies = createMemo(() => {
  const functions = allFunctions();
  const calling = calls().calling;

  const visited: ObjDict<boolean> = {};
  let stack: ObjDict<boolean> = {};

  function dfs(func: FunctionInfo): FunctionInfo[] {
    if (visited[func.name]) {
      if (stack[func.name]) {
        return [func];
      }
      return [];
    }

    visited[func.name] = true;
    stack[func.name] = true;

    const children = calling[func.name] ?? [];
    for (const child of children) {
      const cycle = dfs(child);
      if (cycle.length > 0) {
        return [func, ...cycle];
      }
    }

    stack[func.name] = false;
    return [];
  }

  const cycles = [];
  for (const func of functions) {
    const cycle = dfs(func);
    if (cycle.length > 0) {
      cycles.push(cycle);
      stack = {};
    }
  }

  return cycles;
});

const blockHeight = 80;
const blockWidth = 160;
const verticalSpacing = 80;
const horizontalSpacing = 20;


const graphOf = (func: FunctionInfo) => {
  const calling = calls().calling;

  const depths: ObjDict<number> = {};
  const levels: FunctionInfo[][] = [];

  function assignMaxDepth(f: FunctionInfo, depth: number) {
    depths[f.name] = Math.max(depths[f.name] ?? 0, depth);
    for (const child of calling[f.name] ?? []) {
      assignMaxDepth(child, depth + 1);
    }
  }
  assignMaxDepth(func, 0);

  const visualProps: ObjDict<CallGraphNode> = {};

  const visited: ObjDict<boolean> = {};
  function dfs(f: FunctionInfo) {
    if (visited[f.name]) {
      return;
    }
    visited[f.name] = true;

    const d = depths[f.name];
    levels[d] = levels[d] ?? [];
    levels[d].push(f);

    const y = d * (blockHeight + verticalSpacing);
    const x = (levels[d].length - 1) * (blockWidth + horizontalSpacing);

    visualProps[f.name] = { f, x, y };

    for (const child of calling[f.name] ?? []) {
      dfs(child);
    }
  }
  dfs(func);

  const graphFuncs = Object.values(visualProps).map(pr => pr.f);

  const childFactor = 0.03;
  const parentFactor = 0.04;

  const siblingPushFactor = 0.95;
  const siblingPushDistance = blockWidth + horizontalSpacing / 2;

  for (let iteration = 0; iteration < 1000; iteration++) {
    for (const parent of graphFuncs)
      for (const child of calling[parent.name] ?? []) {
        const dx = visualProps[child.name].x - visualProps[parent.name].x;
        // const dy = visualProps[child.name].y - visualProps[parent.name].y;
        visualProps[child.name].x -= dx * childFactor;
        // visualProps[child.name].y -= dy * childFactor;
        visualProps[parent.name].x += dx * parentFactor;
        // visualProps[parent.name].y += dy * parentFactor;
      }

    for (let levelIteration = 0; levelIteration < 20; levelIteration++)
      for (let li = 0; li < levels.length; li++) {
        const level = levels[li];

        for (let j = 0; j + 1 < level.length; j++) {
          const lname = level[j].name;
          const rname = level[j+1].name;

          const dx = visualProps[rname].x - visualProps[lname].x;
          if (dx >= siblingPushDistance)
            continue;

          const pushDist = (siblingPushDistance - dx) * siblingPushFactor;
          visualProps[lname].x -= pushDist;
          visualProps[rname].x += pushDist;
        }

        const lastFunctionName = level[0].name;
        if (visualProps[lastFunctionName].x < 0)
          visualProps[lastFunctionName].x = 0;
      }
  }

  console.log(func, visualProps)

  return visualProps;
};

function codeContainsLoops(code: string) {
  const forRE = /\bfor\s*\(/;
  const whileRE = /\bwhile\s*\(/;
  const doRE = /\bdo\s*\{/;
  return forRE.test(code) || whileRE.test(code) || doRE.test(code) || code.includes('[&]');
}

createEffect(() => {
  console.log('files:', mFs.files());
  console.log('roots:', roots());
  console.log('calls:', calls());
  console.log('circularDependencies:', circularDependencies());
});



const modifiedMarkSide = 30;
const ModifiedMark = () =>
  <div class="modifiedMark">
    <svg width={modifiedMarkSide} height={modifiedMarkSide}>
      <polygon points={`0, 0, 0, ${modifiedMarkSide}, ${modifiedMarkSide}, 0`} fill="blue" />
    </svg>
  </div>;

const DAGMarkSide = 40;
const DAGRootMark = () =>
  <div class="DAGRootMark">
    <svg width={DAGMarkSide} height={DAGMarkSide}>
      <polygon points={`0, 0, ${DAGMarkSide}, 0, ${DAGMarkSide}, ${DAGMarkSide}`} fill="green" />
    </svg>
  </div>;

const LoopMark = () =>
  <div class="loopMark">[&]</div>;

const CallTree = () => {
  function dfs(func: FunctionInfo) {
    // console.log(func);
    const children = calls().calling[func.name] ?? [];

    return <div class="callTreeOuter">
      ${func.name}
      <div class="callTreeChildren">
        <For each={children}>{child => dfs(child)}</For>
      </div>
    </div>;
  }

  return <div>
    <h3>Call Graph ${mFs.files().length}</h3>
      <For each={roots()}>{root => dfs(root)}</For>
    </div>;
};

const CallGraph = () => {
  return <div>
    <h3>Call Graph</h3>
    <For each={roots().filter(f => f.name === 'start_up')}>{root => { // Dirty ways to get stuff done, hopefully ...
      const gr = graphOf(root);
      if (Object.keys(gr).length === 0) {
        return <div style="padding: 10px; background-color: yellow; border: solid 1px black">${root.name}</div>;
      }

      const containerWidth = Math.max(...Object.values(gr).map(pr => pr.x)) + blockWidth + horizontalSpacing;
      const containerHeight = Math.max(...Object.values(gr).map(pr => pr.y)) + blockHeight;

      return <>
        <div class="callGraphShell">
          <div class="callGraphContainer" style={{
              width: containerWidth + 'px',
              height: containerHeight + 'px',
            }}>
            <svg viewBox={ `0 0 ${containerWidth} ${containerHeight}` } class="callGraphSvg" xmlns="http://www.w3.org/2000/svg">
              <For each={Object.values(gr)}>{pr => 
                <For each={calls().calling[pr.f.name] ?? []}>{child => {
                  const parentPr = gr[pr.f.name];
                  const childPr = gr[child.name];
                  const x1 = parentPr.x + blockWidth / 2;
                  const y1 = parentPr.y + blockHeight / 2;
                  const x2 = childPr.x + blockWidth / 2;
                  const y2 = childPr.y + blockHeight / 2;
                  // return <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#222" stroke-width={2} />;

                  const pathKey = pr.f.name + '->' + child.name;
                  const pathHash = stringHash(pathKey);
                  const pathL = 25 + (pathHash % 20);
                  const pathColor = `hsl(${pathHash % 360}, 100%, ${pathL}%)`;

                  return <path d={`
                    M ${x1} ${y1}
                    C ${x1} ${y1 + (y2 - y1) / 2},
                      ${x2} ${y1 + (y2 - y1) / 2},
                      ${x2} ${y2}
                  `} stroke={pathColor} stroke-width={2} fill="none" />;
                }}</For>
              }</For>
            </svg>
            <For each={Object.values(gr)}>{pr => {
              const { f, x, y } = pr;
              return <div class="callGraphNode"
                classList={{
                  selected: selectedFunction() === f,
                  hasPropertyToHighlight: containsPropertyToHighlight(f),
                }}
                onClick={() => setSelectedFunction(f)}
                style={{
                  left: x + 'px',
                  top: y + 'px',
                  width: blockWidth + 'px',
                  height: blockHeight + 'px',
                }}>
                {modifiedFunctions().includes(f) && <ModifiedMark />}
                {functionsToDAG().includes(f) && <DAGRootMark />}
                {codeContainsLoops(f.code) && <LoopMark />}
                <span class="callGraphNodeTitle">{f.name}</span>
              </div>;
            }}</For>
          </div>
        </div>
        <hr />
      </>;
    }}</For>
  </div>;
};



const FileGroupView = (props: { group: FileGroup, title: string }) => {
  return <div>
    <b>{props.title} files: </b>
    <span>
      <For each={props.group.files()}>{file =>
        <span class="fileLabel">
          {file.name}
          <button onClick={() => props.group.setFiles(props.group.files().filter(f => f !== file))}>-</button>
        </span>
      }</For>
    </span>
    {props.group.errorAutoLoading() && <span style={{ color: 'red' }}>Auto loading previous files failed, click:</span>}
    <button onClick={props.group.loadFiles}>Load previous files</button>
    <button onClick={props.group.openFiles}>Open File(s)</button>
    <button onClick={props.group.reloadFiles}>Reload Files</button>
  </div>;
}



console.warn('propertiesToHighlight:', propertiesToHighlight());

const currentLanguage = createMemo(() => languageVariant(propertyName() ?? '', allFunctions().map(fs => fs.name), propertiesToHighlight()));
const mdLanguage = currentLanguage();
let mdOriginalModel= editor.createModel('', mdLanguage.name);
let mdModifiedModel = editor.createModel('', mdLanguage.name);

createEffect(() => {
  editor.setModelLanguage(mdOriginalModel, currentLanguage().name);
  editor.setModelLanguage(mdModifiedModel, currentLanguage().name);
});

export default function App() {
  let $property;


  const $mainDiffEditorContainer = document.createElement('div');
  {
    $mainDiffEditorContainer.style.height = '70vh';
  
    onMount(() => {
      // https://github.com/microsoft/monaco-editor/blob/35eb0efbc039827432002ccc17b120eb0874d70f/samples/browser-amd-diff-editor/index.html
      var diffEditor = editor.createDiffEditor($mainDiffEditorContainer, { theme: myCppTheme });
      diffEditor.setModel({ original: mdOriginalModel, modified: mdModifiedModel,});

      mdModifiedModel.onDidChangeContent(event => {
        const sf = selectedFunction();
        if (!sf) return;
        sf.editedCode = mdModifiedModel.getValue();
        // ^ Imperative, not "Solid" way, but I have no time for "Solid" way:)

        if (sf.editedCode !== sf.code) {
          if (!modifiedFunctions().includes(sf))
            setModifiedFunctions([...modifiedFunctions(), sf]);
        } else {
          if (modifiedFunctions().includes(sf))
            setModifiedFunctions(modifiedFunctions().filter(mf => mf !== sf));
        }

        // const nextFuncs = [...allFunctions()];
        // const pi = nextFuncs.indexOf(sf);
        // if (pi < 0)
        //   throw `Couldn't find previouslySelectedFunction in allFunctions()`;

        // nextFuncs[pi] = {
        //   ...sf,
        //   editedCode: mdModifiedModel.getValue(),
        // };
        // setAllFunctions(nextFuncs);
      })
    });
  }


  createEffect(() => {
    const sf = selectedFunction();

    mdOriginalModel.setValue(sf?.code ?? '');
    mdModifiedModel.setValue(sf?.editedCode ?? '');
  });



  return <div>
    <FileGroupView group={mFs} title="Main" />
    <FileGroupView group={spFs} title="Special" />

    Property: <input ref={$property} /> <button onClick={() => setProperty($property!.value.trim())}>Analize</button>
    <button onClick={patchEdited}>Patch edited</button>
    <div>
      <textarea rows={10} cols={100}
        autocomplete="off" autocapitalize="off" spellcheck={"false" as unknown as boolean}
        value={functionsToDAGString()}
        onInput={e => setFunctionsToDAGString(e.target.value)}
      ></textarea>
      <button onClick={dagFunctions}>DAG functions</button>
      <textarea rows={10} cols={100}
        autocomplete="off" autocapitalize="off" spellcheck={"false" as unknown as boolean}
        value={propertiesToHighlightString()}
        onInput={e => setPropertiesToHighlightString(e.target.value)}
      ></textarea>
    </div>
    <div><pre>
      Type: {propertyType()} <br />
      Name: <b>{propertyName()}</b> <br />
      {!propertyValid() && 'Property invalid'}
    </pre></div>
    <CallGraph />

    {$mainDiffEditorContainer}

    {propertyName() && <div>
      <For each={analyzeProperty(propertyName()!)}>{func => {
        const file = func.file;
        patchingInfo[file.name] = patchingInfo[file.name] ?? [];

        const language = languageVariant(propertyName()!, allFunctions().map(fs => fs.name), propertiesToHighlight());

        const de = document.createElement('div');
        de.style.height = '70vh';

        const functionCode = file.lines.slice(func.startLine, func.endLine).join('\n');
        let originalModel= editor.createModel(functionCode, language.name);
        let modifiedModel = editor.createModel(functionCode, language.name);

        patchingInfo[func.file.name].push({
          func,
          originalModel,
          modifiedModel,
        });

        setTimeout(() => {
          // https://github.com/microsoft/monaco-editor/blob/35eb0efbc039827432002ccc17b120eb0874d70f/samples/browser-amd-diff-editor/index.html
          var diffEditor = editor.createDiffEditor(de, { theme: myCppTheme });
          diffEditor.setModel({ original: originalModel, modified: modifiedModel,});
        }, 100);

        return <div>
          <h4>${func.file.name}: ${func.name}</h4>
          {de}
        </div>
      }}</For>
      <hr />
      <button onclick={patch}>Patch</button>
    </div>}
    
  </div>;
};
