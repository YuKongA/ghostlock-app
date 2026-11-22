package com.ghostlock.plugin.api.v1;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import org.junit.Test;

public final class PluginApiSurfaceTest {
    @Test
    public void exactTypesAndMembers() throws Exception {
        assertFinalClass(PluginApi.class);
        assertPublicFields(PluginApi.class, Set.of("MAJOR:int", "MINOR:int"));
        assertPublicConstructors(PluginApi.class, Set.of());
        assertPublicMethods(PluginApi.class, Set.of());
        assertEquals(1, PluginApi.MAJOR);
        assertEquals(0, PluginApi.MINOR);

        assertEnum(PluginFailureCode.class,
                "INVALID_INPUT", "NOT_READY", "IO_FAILURE", "CANCELLED", "CONTRACT_FAILURE");
        assertEnum(PluginStageState.class, "SUCCESS", "FAILED", "CANCELLED", "UNKNOWN", "SKIPPED");
        assertEnum(PluginResultReason.class, "NONE", "NOT_STARTED", "REJECTED", "NOT_READY",
                "IO_FAILURE", "PROCESS_EXIT_NONZERO", "SIGNALLED", "CANCELLED",
                "MALFORMED_RESULT", "ARTIFACT_IDENTITY_MISMATCH", "INTERNAL_CONTRACT_FAILURE");
        assertEnum(PluginExitKind.class, "NONE", "PROCESS_EXIT", "NATIVE_STATUS");
        assertEnum(PluginLogOrigin.class, "KOTLIN", "NATIVE", "SHELL");
        assertEnum(PluginLogLevel.class, "DEBUG", "INFO", "WARN", "ERROR");

        assertFinalClass(PluginException.class);
        assertPublicConstructors(PluginException.class,
                Set.of("(PluginFailureCode,String)"));
        assertPublicMethods(PluginException.class,
                Set.of("code():PluginFailureCode", "detail():String"));

        assertInterfaceMethods(DiagnosticBackendEntryPoint.class,
                Set.of("createRun():DiagnosticBackendRun throws PluginException"));
        assertInterfaceMethods(DiagnosticBackendRun.class,
                Set.of("close():void throws PluginException",
                        "execute(DiagnosticRunContext):DiagnosticRunResult throws PluginException"));
        assertFalse(AutoCloseable.class.isAssignableFrom(DiagnosticBackendRun.class));
        assertInterfaceMethods(DiagnosticRunContext.class,
                Set.of("cancellationRequested():boolean", "generation():long",
                        "emit(PluginLogOrigin,PluginLogLevel,String):void", "processPid():int",
                        "processUid():int", "runId():String"));

        assertFinalClass(DiagnosticRunResult.class);
        assertPublicConstructors(DiagnosticRunResult.class,
                Set.of("(PluginStageResult,PluginStageResult)"));
        assertPublicMethods(DiagnosticRunResult.class,
                Set.of("backendPrimitive():PluginStageResult", "rootScriptDispatch():PluginStageResult"));

        assertFinalClass(PluginStageResult.class);
        assertPublicConstructors(PluginStageResult.class,
                Set.of("(PluginStageState,PluginResultReason,PluginExitKind,int)"));
        assertPublicMethods(PluginStageResult.class,
                Set.of("exitKind():PluginExitKind", "exitValue():int", "reason():PluginResultReason",
                        "state():PluginStageState"));
    }

    @Test
    public void checkedExceptionAndBounds() throws Exception {
        assertTrue(Exception.class.isAssignableFrom(PluginException.class));
        assertFalse(RuntimeException.class.isAssignableFrom(PluginException.class));
        PluginException exception = new PluginException(PluginFailureCode.NOT_READY, "尚未准备");
        assertEquals(PluginFailureCode.NOT_READY, exception.code());
        assertEquals("尚未准备", exception.detail());
        assertEquals("尚未准备", exception.getMessage());
        assertThrows(NullPointerException.class, () -> new PluginException(null, "detail"));
        assertThrows(NullPointerException.class,
                () -> new PluginException(PluginFailureCode.NOT_READY, null));
        assertThrows(IllegalArgumentException.class,
                () -> new PluginException(PluginFailureCode.NOT_READY, "line\nbreak"));
        assertThrows(IllegalArgumentException.class,
                () -> new PluginException(PluginFailureCode.NOT_READY, "x".repeat(4097)));
        assertNotNull(new PluginException(PluginFailureCode.IO_FAILURE, "x".repeat(4096)));
    }

    @Test
    public void stageTupleMatrixRejectsEverythingOutsideContract() {
        assertNotNull(stage(PluginStageState.SUCCESS, PluginResultReason.NONE, PluginExitKind.NONE, 0));
        assertNotNull(stage(PluginStageState.SUCCESS, PluginResultReason.NONE, PluginExitKind.PROCESS_EXIT, 0));
        assertNotNull(stage(PluginStageState.FAILED, PluginResultReason.REJECTED, PluginExitKind.NONE, 0));
        assertNotNull(stage(PluginStageState.FAILED, PluginResultReason.IO_FAILURE,
                PluginExitKind.NATIVE_STATUS, -1));
        assertNotNull(stage(PluginStageState.FAILED, PluginResultReason.PROCESS_EXIT_NONZERO,
                PluginExitKind.PROCESS_EXIT, 255));
        assertNotNull(stage(PluginStageState.CANCELLED, PluginResultReason.CANCELLED,
                PluginExitKind.NONE, 0));
        assertNotNull(stage(PluginStageState.UNKNOWN, PluginResultReason.INTERNAL_CONTRACT_FAILURE,
                PluginExitKind.NATIVE_STATUS, 1));
        assertNotNull(stage(PluginStageState.SKIPPED, PluginResultReason.NOT_STARTED,
                PluginExitKind.NONE, 0));

        assertThrows(IllegalArgumentException.class,
                () -> stage(PluginStageState.SUCCESS, PluginResultReason.NONE,
                        PluginExitKind.NATIVE_STATUS, 0));
        assertThrows(IllegalArgumentException.class,
                () -> stage(PluginStageState.FAILED, PluginResultReason.PROCESS_EXIT_NONZERO,
                        PluginExitKind.PROCESS_EXIT, 0));
        assertThrows(IllegalArgumentException.class,
                () -> stage(PluginStageState.UNKNOWN, PluginResultReason.MALFORMED_RESULT,
                        PluginExitKind.NATIVE_STATUS, 0));
        assertThrows(NullPointerException.class,
                () -> new PluginStageResult(null, PluginResultReason.NONE, PluginExitKind.NONE, 0));
    }

    private static PluginStageResult stage(
            PluginStageState state,
            PluginResultReason reason,
            PluginExitKind exitKind,
            int exitValue) {
        return new PluginStageResult(state, reason, exitKind, exitValue);
    }

    private static void assertFinalClass(Class<?> type) {
        assertTrue(Modifier.isPublic(type.getModifiers()));
        assertTrue(Modifier.isFinal(type.getModifiers()));
        assertFalse(type.isInterface());
    }

    private static void assertEnum(Class<? extends Enum<?>> type, String... constants) {
        assertTrue(Modifier.isPublic(type.getModifiers()));
        assertArrayEquals(constants,
                Arrays.stream(type.getEnumConstants()).map(Enum::name).toArray(String[]::new));
        assertPublicConstructors(type, Set.of());
        assertPublicMethods(type, Set.of("valueOf(String):" + type.getSimpleName(),
                "values():" + type.getSimpleName() + "[]"));
    }

    private static void assertInterfaceMethods(Class<?> type, Set<String> expected) {
        assertTrue(type.isInterface());
        assertTrue(Modifier.isPublic(type.getModifiers()));
        assertPublicFields(type, Set.of());
        assertPublicConstructors(type, Set.of());
        assertPublicMethods(type, expected);
    }

    private static void assertPublicFields(Class<?> type, Set<String> expected) {
        Set<String> actual = Arrays.stream(type.getDeclaredFields())
                .filter(field -> Modifier.isPublic(field.getModifiers())
                        || Modifier.isProtected(field.getModifiers()))
                .map(PluginApiSurfaceTest::fieldSignature)
                .collect(Collectors.toSet());
        assertEquals(expected, actual);
    }

    private static void assertPublicConstructors(Class<?> type, Set<String> expected) {
        Set<String> actual = Arrays.stream(type.getDeclaredConstructors())
                .filter(constructor -> Modifier.isPublic(constructor.getModifiers())
                        || Modifier.isProtected(constructor.getModifiers()))
                .map(PluginApiSurfaceTest::constructorSignature)
                .collect(Collectors.toSet());
        assertEquals(expected, actual);
    }

    private static void assertPublicMethods(Class<?> type, Set<String> expected) {
        Set<String> actual = Arrays.stream(type.getDeclaredMethods())
                .filter(method -> Modifier.isPublic(method.getModifiers())
                        || Modifier.isProtected(method.getModifiers()))
                .filter(method -> !method.isSynthetic())
                .map(PluginApiSurfaceTest::methodSignature)
                .collect(Collectors.toSet());
        assertEquals(expected, actual);
    }

    private static String fieldSignature(Field field) {
        return field.getName() + ":" + simpleName(field.getType());
    }

    private static String constructorSignature(Constructor<?> constructor) {
        return "(" + parameterList(constructor.getParameterTypes()) + ")";
    }

    private static String methodSignature(Method method) {
        String signature = method.getName() + "(" + parameterList(method.getParameterTypes()) + "):"
                + simpleName(method.getReturnType());
        List<Class<?>> exceptions = Arrays.asList(method.getExceptionTypes());
        if (!exceptions.isEmpty()) {
            signature += " throws " + exceptions.stream().map(Class::getSimpleName)
                    .collect(Collectors.joining(","));
        }
        return signature;
    }

    private static String parameterList(Class<?>[] parameters) {
        return Arrays.stream(parameters).map(PluginApiSurfaceTest::simpleName)
                .collect(Collectors.joining(","));
    }

    private static String simpleName(Class<?> type) {
        return type.isArray() ? simpleName(type.getComponentType()) + "[]" : type.getSimpleName();
    }
}
