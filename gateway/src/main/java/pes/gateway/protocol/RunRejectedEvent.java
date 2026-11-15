package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class RunRejectedEvent extends ServerEvent {
    private String roomId;
    private String requestId;
    private String reason;
    private String message;

    public RunRejectedEvent() {
        super("run.rejected");
    }

    public RunRejectedEvent(String roomId, String requestId, String reason, String message) {
        super("run.rejected");
        this.roomId = roomId;
        this.requestId = requestId;
        this.reason = reason;
        this.message = message;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public String getRequestId() {
        return requestId;
    }

    public void setRequestId(String requestId) {
        this.requestId = requestId;
    }

    public String getReason() {
        return reason;
    }

    public void setReason(String reason) {
        this.reason = reason;
    }

    public String getMessage() {
        return message;
    }

    public void setMessage(String message) {
        this.message = message;
    }
}
