/*
 * Test the C API constructors for stack switching: `cont.new`, `suspend`,
 * `resume` (with and without handler clauses), `resume_throw`, and the
 * continuation type a TypeBuilder has to be able to name.
 *
 * The program is the smallest one that exercises all of them: `body` suspends,
 * `go` creates a continuation for it, resumes it with an `(on $yield $parked)`
 * handler, and — once the suspension has been handled — resumes the parked
 * continuation to completion.
 */

#include <binaryen-c.h>
#include <stdio.h>

static BinaryenExpressionRef i32(BinaryenModuleRef module, int32_t x) {
  return BinaryenConst(module, BinaryenLiteralInt32(x));
}

int main() {
  BinaryenModuleRef module = BinaryenModuleCreate();
  BinaryenModuleSetFeatures(module,
                            BinaryenFeatureReferenceTypes() |
                              BinaryenFeatureExceptionHandling() |
                              BinaryenFeatureGC() |
                              BinaryenFeatureStackSwitching());

  // $ft = (func), $ct = (cont $ft)
  TypeBuilderRef builder = TypeBuilderCreate(2);
  TypeBuilderSetSignatureType(builder, 0, BinaryenTypeNone(), BinaryenTypeNone());
  BinaryenHeapType ft = TypeBuilderGetTempHeapType(builder, 0);
  TypeBuilderSetContinuation(builder, 1, ft);
  BinaryenHeapType heapTypes[2];
  BinaryenIndex errorIndex = 0;
  TypeBuilderErrorReason errorReason;
  if (!TypeBuilderBuildAndDispose(builder, heapTypes, &errorIndex, &errorReason)) {
    printf("TypeBuilder failed at %u\n", errorIndex);
    return 1;
  }
  BinaryenHeapType ftHeap = heapTypes[0];
  BinaryenHeapType ct = heapTypes[1];
  BinaryenType ctRef = BinaryenTypeFromHeapType(ct, true);

  BinaryenAddTag(module, "yield", BinaryenTypeNone(), BinaryenTypeNone());

  // (func $body (suspend $yield))
  BinaryenAddFunction(module,
                      "body",
                      BinaryenTypeNone(),
                      BinaryenTypeNone(),
                      NULL,
                      0,
                      BinaryenSuspend(module, "yield", NULL, 0));

  // The function reference `ref.func $body` needs a declaring element segment.
  const char* declared[1] = {"body"};
  BinaryenAddPassiveElementSegment(module, "declare", declared, 1);

  // (func $go (result i32) (local $k (ref null $ct)) ...)
  BinaryenType vars[1] = {ctRef};
  BinaryenExpressionRef children[2];
  // (block $parked (result (ref null $ct))
  //   (resume $ct (on $yield $parked) (cont.new $ct (ref.func $body)))
  //   (return (i32.const 0)))
  children[0] = BinaryenResumeWithHandlers(
    module,
    BinaryenContNew(module,
                    ct,
                    BinaryenRefFunc(module, "body", ftHeap)),
    ct,
    (const char*[]){"yield"},
    (const char*[]){[0] = "parked"},
    1);
  children[1] = BinaryenReturn(module, i32(module, 0));
  BinaryenExpressionRef parked = BinaryenBlock(module, "parked", children, 2, ctRef);

  // (resume $ct (local.get $k)) (i32.const 1)
  BinaryenExpressionRef body[3];
  body[0] = BinaryenLocalSet(module, 0, parked);
  body[1] = BinaryenResume(module, BinaryenLocalGet(module, 0, ctRef), ct);
  body[2] = i32(module, 1);
  BinaryenAddFunction(module,
                      "go",
                      BinaryenTypeNone(),
                      BinaryenTypeInt32(),
                      vars,
                      1,
                      BinaryenBlock(module, NULL, body, 3, BinaryenTypeInt32()));
  BinaryenAddFunctionExport(module, "go", "go");

  BinaryenModulePrint(module);
  printf("\nvalidate: %d\n", BinaryenModuleValidate(module));

  BinaryenModuleDispose(module);

  // `resume_throw`, and `suspend` with a payload, which the program above does not
  // need. A second module keeps the printed one readable; only its validity is
  // reported, because the point is that the constructors can be called at all.
  BinaryenModuleRef thrower = BinaryenModuleCreate();
  BinaryenModuleSetFeatures(thrower,
                            BinaryenFeatureReferenceTypes() |
                              BinaryenFeatureExceptionHandling() |
                              BinaryenFeatureGC() |
                              BinaryenFeatureStackSwitching());
  TypeBuilderRef builder2 = TypeBuilderCreate(2);
  TypeBuilderSetSignatureType(builder2, 0, BinaryenTypeNone(), BinaryenTypeNone());
  BinaryenHeapType ft2 = TypeBuilderGetTempHeapType(builder2, 0);
  TypeBuilderSetContinuation(builder2, 1, ft2);
  BinaryenHeapType heapTypes2[2];
  TypeBuilderBuildAndDispose(builder2, heapTypes2, &errorIndex, &errorReason);
  BinaryenHeapType ct2 = heapTypes2[1];

  BinaryenAddTag(thrower, "cancel", BinaryenTypeInt32(), BinaryenTypeNone());
  BinaryenExpressionRef payload[1] = {i32(thrower, 1)};
  BinaryenAddFunction(thrower,
                      "body2",
                      BinaryenTypeNone(),
                      BinaryenTypeNone(),
                      NULL,
                      0,
                      BinaryenSuspend(thrower, "cancel", payload, 1));
  const char* declared2[1] = {"body2"};
  BinaryenAddPassiveElementSegment(thrower, "declare2", declared2, 1);

  // (func $go2 (result i32)
  //   (resume_throw $ct2 $cancel (i32.const 1) (cont.new $ct2 (ref.func $body2)))
  //   (i32.const 2))
  BinaryenExpressionRef body2[2];
  body2[0] = BinaryenResumeThrow(thrower,
                                 BinaryenContNew(thrower,
                                                 ct2,
                                                 BinaryenRefFunc(thrower,
                                                                 "body2",
                                                                 heapTypes2[0])),
                                 ct2,
                                 "cancel",
                                 payload,
                                 1);
  body2[1] = i32(thrower, 2);
  BinaryenAddFunction(thrower,
                      "go2",
                      BinaryenTypeNone(),
                      BinaryenTypeInt32(),
                      NULL,
                      0,
                      BinaryenBlock(thrower, NULL, body2, 2, BinaryenTypeInt32()));
  printf("resume_throw validate: %d\n", BinaryenModuleValidate(thrower));
  BinaryenModuleDispose(thrower);
  return 0;
}
