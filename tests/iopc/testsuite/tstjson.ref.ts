/***** THIS FILE IS AUTOGENERATED DO NOT MODIFY DIRECTLY ! *****/
/* tslint:disable */
/* eslint-disable */

export type MyEnumA_Int = 0
    | 1
    | 100
    | 101;
export type MyEnumA_Str = 'A'
    | 'B'
    | 'C'
    | 'D';
export type MyEnumA = MyEnumA_Str;

export type MyEnumB_Int = 0
    | 1
    | 2;
export type MyEnumB_Str = 'A'
    | 'B'
    | 'C';
export type MyEnumB = MyEnumB_Str;

export type MyEnumC_Int = 0
    | 1;
export type MyEnumC_Str = 'A'
    | 'B';
export type MyEnumC = MyEnumC_Str;

export type MyUnionA = { i: number }
    | { b: number }
    | { s: string };
export type MyUnionA_Pairs = { kind: 'i', value: number }
    | { kind: 'b', value: number }
    | { kind: 's', value: string };
export type MyUnionA_Keys = 'i' | 'b' | 's';

const MyStructA_fullname = 'tstjson.MyStructA';
export interface MyStructA {
    i?: number;
    s?: string;
    u1: MyUnionA;
    u2: MyUnionA;
    u3: MyUnionA;
    class: number;
}

const MyStructB_fullname = 'tstjson.MyStructB';
export interface MyStructB {
    a: number;
    b: number;
    c: number;
    d: number;
    e: number;
    f: number;
    g: number | string;
    h: number | string;
    i: number;
    j: boolean;
    k: string;
    l: string;
    m: MyEnumA;
    unionA: Array<MyUnionA>;
    structA: MyStructA;
    xmlField: string;
}

const MyStructC_fullname = 'tstjson.MyStructC';
export interface MyStructC {
    a: number;
    b: number;
}


export type ConstraintU = { u8: number }
    | { u16: number }
    | { u32: number }
    | { u64: number | string }
    | { s: string };
export type ConstraintU_Pairs = { kind: 'u8', value: number }
    | { kind: 'u16', value: number }
    | { kind: 'u32', value: number }
    | { kind: 'u64', value: number | string }
    | { kind: 's', value: string };
export type ConstraintU_Keys = 'u8' | 'u16' | 'u32' | 'u64' | 's';

const ConstraintS_fullname = 'tstjson.ConstraintS';
export interface ConstraintS {
    i8: Array<number>;
    i16: Array<number>;
    i32: Array<number>;
    i64: Array<number | string>;
    s: Array<string>;
    s2: string;
}

const MyClassBase_fullname = 'tstjson.MyClassBase';
export interface MyClassBase {
    _class: string;
    a: number;
    structA: MyStructA;
}

const MyClassA_fullname = 'tstjson.MyClassA';
export interface MyClassA extends MyClassBase {
    b?: number;
    structB: Array<MyStructB>;
}

const MyClassB_fullname = 'tstjson.MyClassB';
export interface MyClassB extends MyClassBase {
    val: number;
}

const MyClass1_fullname = 'tstjson.MyClass1';
export interface MyClass1 {
    _class: string;
}

const MyClass2_fullname = 'tstjson.MyClass2';
export interface MyClass2 extends MyClass1 {
}

const ClassContainer_fullname = 'tstjson.ClassContainer';
export interface ClassContainer {
    a: MyClassA;
    b: MyClassB;
}

const MyExceptionA_fullname = 'tstjson.MyExceptionA';
export interface MyExceptionA {
    errcode: number;
    desc: string;
}


export namespace interfaces {
    export namespace MyIfaceA {
        const funAArgs_fullname = 'tstjson.MyIfaceA.funAArgs';
        export interface funAArgs {
            a: number;
            b: MyStructA;
        }
        const funARes_fullname = 'tstjson.MyIfaceA.funARes';
        export interface funARes {
            c: MyUnionA;
            d: number;
        }
        export type funAExn = void;
        export type funA = (a: number, b: MyStructA) => Promise<funARes>;

        export type funbArgs = void;
        const funbRes_fullname = 'tstjson.MyIfaceA.funbRes';
        export interface funbRes {
            a: number;
            b: number;
        }
        export type funbExn = void;
        export type funb = () => Promise<funbRes>;

        export type funDArgs = void;
        export type funDRes = MyStructA;
        export type funDExn = void;
        export type funD = () => Promise<funDRes>;

        export type funCArgs = MyStructA;
        export type funCRes = MyStructB;
        export type funCExn = void;
        export type funC = (arg: MyStructA) => Promise<funCRes>;

        const funFArgs_fullname = 'tstjson.MyIfaceA.funFArgs';
        export interface funFArgs {
            a: Array<number>;
            b?: number;
        }
        export type funFRes = MyStructB;
        export type funFExn = void;
        export type funF = (a: Array<number>, b: number | undefined) => Promise<funFRes>;

        export type funGArgs = void;
        export type funGRes = void;
        export type funGExn = void;
        export type funG = () => Promise<void>;

        export type funHArgs = void;
        export type funHRes = void;
        export type funHExn = void;
        export type funH = () => Promise<void>;

        export type funIArgs = MyUnionA;
        export type funIRes = void;
        export type funIExn = void;
        export type funI = (arg: MyUnionA) => Promise<void>;

        export type funJArgs = MyStructA;
        const funJRes_fullname = 'tstjson.MyIfaceA.funJRes';
        export interface funJRes {
            a: number;
            b: number;
        }
        const funJExn_fullname = 'tstjson.MyIfaceA.funJExn';
        export interface funJExn {
            err: number;
        }
        export type funJ = (arg: MyStructA) => Promise<funJRes>;

        export type funEArgs = void;
        const funERes_fullname = 'tstjson.MyIfaceA.funERes';
        export interface funERes {
            a?: number;
        }
        export type funEExn = void;
        export type funE = () => Promise<funERes>;

        export type funKArgs = MyStructA;
        const funKRes_fullname = 'tstjson.MyIfaceA.funKRes';
        export interface funKRes {
            a: number;
            b: number;
        }
        export type funKExn = MyExceptionA;
        export type funK = (arg: MyStructA) => Promise<funKRes>;

        const funLArgs_fullname = 'tstjson.MyIfaceA.funLArgs';
        export interface funLArgs {
            a: number;
            b: number;
            c: number;
        }
        export type funLRes = void;
        export type funLExn = void;
        export type funL = (a: number, b: number, c: number) => Promise<void>;

        const funAsyncArgs_fullname = 'tstjson.MyIfaceA.funAsyncArgs';
        export interface funAsyncArgs {
            type: number;
        }
        export type funAsyncRes = void;
        export type funAsyncExn = void;
        export type funAsync = (type: number) => void;

    }
    export namespace MyIfaceB {
        const funAArgs_fullname = 'tstjson.MyIfaceB.funAArgs';
        export interface funAArgs {
            i: number;
        }
        const funARes_fullname = 'tstjson.MyIfaceB.funARes';
        export interface funARes {
            i: number;
        }
        export type funAExn = void;
        export type funA = (i: number) => Promise<funARes>;

    }
    export namespace MyIfaceC {
        const funAArgs_fullname = 'tstjson.MyIfaceC.funAArgs';
        export interface funAArgs {
            i: number;
        }
        const funARes_fullname = 'tstjson.MyIfaceC.funARes';
        export interface funARes {
            i: number;
        }
        export type funAExn = void;
        export type funA = (i: number) => Promise<funARes>;

    }
    export namespace MyIfaceD {
        const funAArgs_fullname = 'tstjson.MyIfaceD.funAArgs';
        export interface funAArgs {
            i: number;
        }
        const funARes_fullname = 'tstjson.MyIfaceD.funARes';
        export interface funARes {
            i: number;
        }
        export type funAExn = void;
        export type funA = (i: number) => Promise<funARes>;

    }
}