import { onCleanup, createSignal, createEffect, createMemo, For, onMount } from "solid-js";
import { editor, languages } from 'monaco-editor';
import { get as dbGet, set as dbSet } from 'idb-keyval';


import "./app.css";
import { myCppRules } from "./myCpp_rules";


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

function languageVariant(targetProperty: string, functionNames: string[]) {
  const key = targetProperty + '___' + functionNames.toSorted().join(',');

  if (!cache[key]) {
    const res = {
      name: 'myCpp_' + key,
      definition: {
        ...myCppRules.language,
        toHighlight: [key],
        functionsToHighlight: functionNames,
      },
    };
    cache[key] = res;

    languages.register({ id: res.name });
    languages.setMonarchTokensProvider(res.name, res.definition);

  }
  
  return cache[key];
}



async function loadedFiles(): Promise<[boolean, MyFile[]]> {
  const loadedPartialFiles = await dbGet('files');
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

let [errorAutoLoading, setErrorAutoLoading] = createSignal(!__initialState[0]);
let [files, setFilesPrivate] = createSignal<MyFile[]>(__initialState[1]);
let [allFunctions, setAllFunctions] = createSignal<FunctionInfo[]>([]);
let [property, setProperty] = createSignal('');
let [selectedFunction, setSelectedFunction] = createSignal<null | FunctionInfo>(null);
let [modifiedFunctions, setModifiedFunctions] = createSignal<FunctionInfo[]>([]);
let previouslySelectedFunction: null | FunctionInfo = null;

let propertyType = () => property().split(' ').slice(0, -1).join(' ').trim();
let propertyName = () => property().split(' ').at(-1);
let propertyValid = () => (propertyType() && propertyName() && true);

function setFiles(newFiles: MyFile[]) {
  setErrorAutoLoading(false);
  setFilesPrivate(newFiles);
  dbSet('files', newFiles.map(f => {
    const { handle } = f;
    return { handle };
  }));
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
  const loadedPartialFiles = await dbGet('files');
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

async function patch() {
  let patchedSomething = false;

  const nextFiles = [];

  for (const file of files()) {
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
      const writable = await file.handle.createWritable();
      await writable.write(patchedCode);
      await writable.close();

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

  setFiles(nextFiles);
};

async function patchEdited() {
  let patchedSomething = false;
  const nextFiles = [];

  for (const file of files()) {
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
      const writable = await file.handle.createWritable();
      await writable.write(patchedCode);
      await writable.close();

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
    return;
  }
  console.log('patchedSomething:', patchedSomething);

  setFiles(nextFiles);
}


createEffect(function extractFunctionsFromFiles() {
  let functions: FunctionInfo[] = [];
  // {
    // 	name,
    //	file,
    // 	startLine,
    // 	endLine,
  // };

  for (const file of files()) {
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

const calls = createMemo(() => {
  const functions = allFunctions();
  const calling: ObjDict<FunctionInfo[]> = {};
  const calledFrom: ObjDict<FunctionInfo[]> = {};

  for (const inner of functions) {
    const regex = new RegExp(`\\b${inner.name}\\b`, 'g');
    for (const outer of functions) {
      if (outer !== inner && regex.test(outer.code)) {
        calling[outer.name] = [...(calling[outer.name] ?? []), inner];
        calledFrom[inner.name] = [...(calledFrom[inner.name] ?? []), outer];
      }
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
const verticalSpacing = 50;
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

createEffect(() => {
  console.log('files:', files());
  console.log('roots:', roots());
  console.log('calls:', calls());
  console.log('circularDependencies:', circularDependencies());
});



const markSide = 30;
const ModifiedMark = () =>
  <div class="modifiedMark">
    <svg width={markSide} height={markSide}>
      <polygon points={`0, 0, 0, ${markSide}, ${markSide}, 0`} fill="blue" />
    </svg>
  </div>;

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
    <h3>Call Graph ${files().length}</h3>
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

      const containerWidth = Math.max(...Object.values(gr).map(pr => pr.x)) + blockWidth;
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
                  return <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#222" stroke-width={2} />;
                }}</For>
              }</For>
            </svg>
            <For each={Object.values(gr)}>{pr => {
              const { f, x, y } = pr;
              return <div class="callGraphNode"
                classList={{
                  selected: selectedFunction() === f,
                }}
                onClick={() => setSelectedFunction(f)}
                style={{
                  left: x + 'px',
                  top: y + 'px',
                  width: blockWidth + 'px',
                  height: blockHeight + 'px',
                }}>
                {modifiedFunctions().includes(f) && <ModifiedMark />}
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





export default function App() {
  let $property;


  const mdLanguage = languageVariant(propertyName() ?? '', allFunctions().map(fs => fs.name));
  let mdOriginalModel= editor.createModel('', mdLanguage.name);
  let mdModifiedModel = editor.createModel('', mdLanguage.name);
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
    <span>
      <For each={files()}>{file =>
        <span class="fileLabel">
          {file.name}
          <button onClick={() => setFiles(files().filter(f => f !== file))}>-</button>
        </span>
      }</For>
    </span>
    {errorAutoLoading() && <span style={{ color: 'red' }}>Auto loading previous files failed, click:</span>}
    <button onClick={loadFiles}>Load previous files</button>
    <button onClick={openFiles}>Open File(s)</button>
    <button onClick={reloadFiles}>Reload Files</button>
    Property: <input ref={$property} /> <button onClick={() => setProperty($property!.value.trim())}>Analize</button>
    <button onClick={patchEdited}>Patch edited</button>
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

        const language = languageVariant(propertyName()!, allFunctions().map(fs => fs.name));

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
