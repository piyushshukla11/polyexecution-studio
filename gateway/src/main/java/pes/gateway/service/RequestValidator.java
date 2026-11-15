package pes.gateway.service;

import org.springframework.stereotype.Component;
import pes.execution.v1.Language;
import pes.execution.v1.ResourceLimits;
import pes.gateway.config.GatewayProperties;
import pes.gateway.protocol.ResourceLimitsDto;
import pes.gateway.protocol.RunMessage;

import java.nio.charset.StandardCharsets;
import java.util.regex.Pattern;

@Component
public class RequestValidator {
    private static final Pattern ID_PATTERN = Pattern.compile("^[a-zA-Z0-9_-]{1,64}$");

    private static final long DEFAULT_CPU_MS = 2000L;
    private static final long MIN_CPU_MS = 100L;
    private static final long MAX_CPU_MS = 10000L;

    private static final long DEFAULT_WALL_MS = 5000L;
    private static final long MIN_WALL_MS = 100L;
    private static final long MAX_WALL_MS = 15000L;

    private static final long DEFAULT_MEMORY_MB = 64L;
    private static final long MIN_MEMORY_MB = 16L;
    private static final long MAX_MEMORY_MB = 512L;

    private static final long DEFAULT_MAX_OUTPUT_KB = 1024L;
    private static final long MIN_MAX_OUTPUT_KB = 16L;
    private static final long MAX_MAX_OUTPUT_KB = 8192L;

    private final GatewayProperties properties;

    public RequestValidator(GatewayProperties properties) {
        this.properties = properties;
    }

    public boolean isValidId(String id) {
        return id != null && ID_PATTERN.matcher(id).matches();
    }

    public ValidationResult validateRunRequest(RunMessage run) {
        if (run == null) {
            return ValidationResult.error("INVALID_REQUEST", "Run request payload is missing");
        }

        if (!isValidId(run.getRoomId())) {
            return ValidationResult.error("INVALID_ROOM_ID", "Room ID must match ^[a-zA-Z0-9_-]{1,64}$");
        }

        if (!isValidId(run.getRequestId())) {
            return ValidationResult.error("INVALID_REQUEST_ID", "Request ID must match ^[a-zA-Z0-9_-]{1,64}$");
        }

        if (run.getLanguage() == null || run.getLanguage().isBlank()) {
            return ValidationResult.error("UNSUPPORTED_LANGUAGE", "Language is required (must be 'c' or 'python')");
        }

        Language protoLanguage;
        String langLower = run.getLanguage().trim().toLowerCase();
        if ("c".equals(langLower)) {
            protoLanguage = Language.LANGUAGE_C;
        } else if ("python".equals(langLower)) {
            protoLanguage = Language.LANGUAGE_PYTHON;
        } else {
            return ValidationResult.error("UNSUPPORTED_LANGUAGE",
                "Unsupported language '" + run.getLanguage() + "'. Allowed languages are 'c' and 'python'");
        }

        if (run.getSourceCode() == null || run.getSourceCode().isEmpty()) {
            return ValidationResult.error("EMPTY_SOURCE", "Source code cannot be empty");
        }

        byte[] sourceBytes = run.getSourceCode().getBytes(StandardCharsets.UTF_8);
        if (sourceBytes.length > properties.getMaxSourceBytes()) {
            return ValidationResult.error("SOURCE_TOO_LARGE",
                "Source code size " + sourceBytes.length + " bytes exceeds limit of " + properties.getMaxSourceBytes() + " bytes");
        }

        if (run.getStdin() != null) {
            byte[] stdinBytes = run.getStdin().getBytes(StandardCharsets.UTF_8);
            if (stdinBytes.length > properties.getMaxStdinBytes()) {
                return ValidationResult.error("STDIN_TOO_LARGE",
                    "Stdin size " + stdinBytes.length + " bytes exceeds limit of " + properties.getMaxStdinBytes() + " bytes");
            }
        }

        ResourceLimitsDto limitsDto = run.getLimits();
        long cpuMs = (limitsDto != null && limitsDto.getCpuMs() != null) ? limitsDto.getCpuMs() : DEFAULT_CPU_MS;
        long wallMs = (limitsDto != null && limitsDto.getWallMs() != null) ? limitsDto.getWallMs() : DEFAULT_WALL_MS;
        long memoryMb = (limitsDto != null && limitsDto.getMemoryMb() != null) ? limitsDto.getMemoryMb() : DEFAULT_MEMORY_MB;
        long maxOutputKb = (limitsDto != null && limitsDto.getMaxOutputKb() != null) ? limitsDto.getMaxOutputKb() : DEFAULT_MAX_OUTPUT_KB;

        if (cpuMs < MIN_CPU_MS || cpuMs > MAX_CPU_MS) {
            return ValidationResult.error("INVALID_LIMITS",
                "cpuMs " + cpuMs + " is out of bounds [" + MIN_CPU_MS + ", " + MAX_CPU_MS + "]");
        }
        if (wallMs < MIN_WALL_MS || wallMs > MAX_WALL_MS) {
            return ValidationResult.error("INVALID_LIMITS",
                "wallMs " + wallMs + " is out of bounds [" + MIN_WALL_MS + ", " + MAX_WALL_MS + "]");
        }
        if (memoryMb < MIN_MEMORY_MB || memoryMb > MAX_MEMORY_MB) {
            return ValidationResult.error("INVALID_LIMITS",
                "memoryMb " + memoryMb + " is out of bounds [" + MIN_MEMORY_MB + ", " + MAX_MEMORY_MB + "]");
        }
        if (maxOutputKb < MIN_MAX_OUTPUT_KB || maxOutputKb > MAX_MAX_OUTPUT_KB) {
            return ValidationResult.error("INVALID_LIMITS",
                "maxOutputKb " + maxOutputKb + " is out of bounds [" + MIN_MAX_OUTPUT_KB + ", " + MAX_MAX_OUTPUT_KB + "]");
        }

        ResourceLimits protoLimits = ResourceLimits.newBuilder()
            .setCpuMs(cpuMs)
            .setWallMs(wallMs)
            .setMemoryMb(memoryMb)
            .setMaxOutputKb(maxOutputKb)
            .build();

        return ValidationResult.success(protoLanguage, protoLimits);
    }
}
