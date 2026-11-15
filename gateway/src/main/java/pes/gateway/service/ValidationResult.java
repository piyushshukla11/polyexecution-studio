package pes.gateway.service;

import pes.execution.v1.Language;
import pes.execution.v1.ResourceLimits;

public record ValidationResult(
    boolean valid,
    String reason,
    String message,
    Language protoLanguage,
    ResourceLimits protoLimits
) {
    public static ValidationResult success(Language protoLanguage, ResourceLimits protoLimits) {
        return new ValidationResult(true, null, null, protoLanguage, protoLimits);
    }

    public static ValidationResult error(String reason, String message) {
        return new ValidationResult(false, reason, message, null, null);
    }
}
