package pes.gateway;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import pes.execution.v1.Language;
import pes.gateway.config.GatewayProperties;
import pes.gateway.protocol.ResourceLimitsDto;
import pes.gateway.protocol.RunMessage;
import pes.gateway.service.RequestValidator;
import pes.gateway.service.ValidationResult;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.*;

class RequestValidatorTest {

    private RequestValidator validator;
    private GatewayProperties properties;

    @BeforeEach
    void setUp() {
        properties = new GatewayProperties();
        validator = new RequestValidator(properties);
    }

    @Test
    @DisplayName("Valid C run request passes validation with default limits")
    void testValidCRunRequest() {
        RunMessage run = new RunMessage("room-1", "req-1", "c", "int main() { return 0; }", "", null);
        ValidationResult result = validator.validateRunRequest(run);

        assertTrue(result.valid());
        assertEquals(Language.LANGUAGE_C, result.protoLanguage());
        assertNotNull(result.protoLimits());
        assertEquals(2000L, result.protoLimits().getCpuMs());
        assertEquals(5000L, result.protoLimits().getWallMs());
        assertEquals(64L, result.protoLimits().getMemoryMb());
        assertEquals(1024L, result.protoLimits().getMaxOutputKb());
    }

    @Test
    @DisplayName("Valid Python run request passes validation with custom limits")
    void testValidPythonRunRequest() {
        ResourceLimitsDto limits = new ResourceLimitsDto(1500L, 4000L, 128L, 2048L);
        RunMessage run = new RunMessage("room_abc", "req_xyz", "python", "print('hello')", "sample input", limits);
        ValidationResult result = validator.validateRunRequest(run);

        assertTrue(result.valid());
        assertEquals(Language.LANGUAGE_PYTHON, result.protoLanguage());
        assertNotNull(result.protoLimits());
        assertEquals(1500L, result.protoLimits().getCpuMs());
        assertEquals(4000L, result.protoLimits().getWallMs());
        assertEquals(128L, result.protoLimits().getMemoryMb());
        assertEquals(2048L, result.protoLimits().getMaxOutputKb());
    }

    @Test
    @DisplayName("Invalid language is rejected with UNSUPPORTED_LANGUAGE")
    void testInvalidLanguageRejected() {
        RunMessage run = new RunMessage("room-1", "req-1", "rust", "fn main() {}", "", null);
        ValidationResult result = validator.validateRunRequest(run);

        assertFalse(result.valid());
        assertEquals("UNSUPPORTED_LANGUAGE", result.reason());
    }

    @Test
    @DisplayName("Malformed room ID is rejected")
    void testMalformedRoomIdRejected() {
        assertFalse(validator.isValidId("room/invalid"));
        assertFalse(validator.isValidId("room space"));
        assertFalse(validator.isValidId("room!@#"));
        assertFalse(validator.isValidId(""));
        assertFalse(validator.isValidId(null));
        assertFalse(validator.isValidId("a".repeat(65)));

        assertTrue(validator.isValidId("room-1"));
        assertTrue(validator.isValidId("room_123"));
        assertTrue(validator.isValidId("ROOM-ABC_xyz-0"));
    }

    @Test
    @DisplayName("Empty source code is rejected")
    void testEmptySourceCodeRejected() {
        RunMessage run = new RunMessage("room-1", "req-1", "c", "", "", null);
        ValidationResult result = validator.validateRunRequest(run);

        assertFalse(result.valid());
        assertEquals("EMPTY_SOURCE", result.reason());
    }

    @Test
    @DisplayName("Oversized source code (>256 KiB) is rejected")
    void testOversizedSourceCodeRejected() {
        String largeSource = "a".repeat(256 * 1024 + 1);
        RunMessage run = new RunMessage("room-1", "req-1", "c", largeSource, "", null);
        ValidationResult result = validator.validateRunRequest(run);

        assertFalse(result.valid());
        assertEquals("SOURCE_TOO_LARGE", result.reason());
    }

    @Test
    @DisplayName("Oversized stdin (>64 KiB) is rejected")
    void testOversizedStdinRejected() {
        String largeStdin = "x".repeat(64 * 1024 + 1);
        RunMessage run = new RunMessage("room-1", "req-1", "c", "int main(){}", largeStdin, null);
        ValidationResult result = validator.validateRunRequest(run);

        assertFalse(result.valid());
        assertEquals("STDIN_TOO_LARGE", result.reason());
    }

    @Test
    @DisplayName("Out of bounds limits are rejected")
    void testOutOfBoundsLimitsRejected() {
        // CPU limit too low
        RunMessage r1 = new RunMessage("room-1", "req-1", "c", "int main(){}", "", new ResourceLimitsDto(50L, 5000L, 64L, 1024L));
        assertFalse(validator.validateRunRequest(r1).valid());

        // CPU limit too high
        RunMessage r2 = new RunMessage("room-1", "req-1", "c", "int main(){}", "", new ResourceLimitsDto(20000L, 5000L, 64L, 1024L));
        assertFalse(validator.validateRunRequest(r2).valid());

        // Wall limit too low
        RunMessage r3 = new RunMessage("room-1", "req-1", "c", "int main(){}", "", new ResourceLimitsDto(1000L, 50L, 64L, 1024L));
        assertFalse(validator.validateRunRequest(r3).valid());

        // Memory limit too high
        RunMessage r4 = new RunMessage("room-1", "req-1", "c", "int main(){}", "", new ResourceLimitsDto(1000L, 5000L, 1024L, 1024L));
        assertFalse(validator.validateRunRequest(r4).valid());
    }

    @Test
    @DisplayName("Valid Java, Kotlin, and Node requests pass validation")
    void testMultiLanguageValidation() {
        RunMessage javaRun = new RunMessage("room-1", "req-1", "java", "class Main { public static void main(String[] args) {} }", "", null);
        ValidationResult javaRes = validator.validateRunRequest(javaRun);
        assertTrue(javaRes.valid());
        assertEquals(Language.LANGUAGE_JAVA, javaRes.protoLanguage());

        RunMessage kotlinRun = new RunMessage("room-1", "req-2", "kotlin", "fun main() {}", "", null);
        ValidationResult kotlinRes = validator.validateRunRequest(kotlinRun);
        assertTrue(kotlinRes.valid());
        assertEquals(Language.LANGUAGE_KOTLIN, kotlinRes.protoLanguage());

        RunMessage nodeRun = new RunMessage("room-1", "req-3", "node", "console.log(1)", "", null);
        ValidationResult nodeRes = validator.validateRunRequest(nodeRun);
        assertTrue(nodeRes.valid());
        assertEquals(Language.LANGUAGE_NODE, nodeRes.protoLanguage());

        RunMessage jsRun = new RunMessage("room-1", "req-4", "javascript", "console.log(2)", "", null);
        ValidationResult jsRes = validator.validateRunRequest(jsRun);
        assertTrue(jsRes.valid());
        assertEquals(Language.LANGUAGE_NODE, jsRes.protoLanguage());
    }
}
